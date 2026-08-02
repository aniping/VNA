param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot
)

$ErrorActionPreference = 'Stop'
$release = (Resolve-Path -LiteralPath $ReleaseRoot).Path
$serverPath = (Resolve-Path -LiteralPath (
    Join-Path $release 'bin\vna-server.exe')).Path
$existingServer = @(Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -eq $serverPath
})
if ($existingServer) {
    throw 'The exact release server is already running; refusing unsafe cleanup'
}
$listener = Get-NetTCPConnection -LocalPort 8080 -State Listen `
    -ErrorAction SilentlyContinue
if ($listener) {
    throw "Port 8080 is already owned by PID $($listener.OwningProcess)"
}

$artifacts = Join-Path ([IO.Path]::GetTempPath()) (
    'vna-release-smoke-' + [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds())
$outside = Join-Path $artifacts 'outside'
New-Item -ItemType Directory -Path $outside -Force | Out-Null
$stdout = Join-Path $artifacts 'stdout.txt'
$stderr = Join-Path $artifacts 'stderr.txt'
$launcher = $null
$server = $null

function Wait-ForHealth([Diagnostics.Process]$process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        try {
            return Invoke-WebRequest -UseBasicParsing `
                'http://127.0.0.1:8080/api/v1/health' -TimeoutSec 1
        } catch {
            if ($process.HasExited) { return $null }
            Start-Sleep -Milliseconds 100
        }
    } while ([DateTime]::UtcNow -lt $deadline)
    return $null
}

function Read-Text([string]$path) {
    if (Test-Path -LiteralPath $path) {
        return Get-Content -LiteralPath $path -Raw
    }
    return ''
}

function Stop-ExactReleaseServer {
    $candidates = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -eq $serverPath })
    if ($candidates.Count -gt 1) {
        throw "Refusing to stop $($candidates.Count) release server processes"
    }
    if ($candidates.Count -eq 0) { return }

    $processId = $candidates[0].ProcessId
    $confirmed = Get-CimInstance Win32_Process `
        -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue
    if (-not $confirmed -or $confirmed.ExecutablePath -ne $serverPath) {
        throw 'Release server PID changed before cleanup'
    }
    $process = Get-Process -Id $processId -ErrorAction Stop
    Stop-Process -InputObject $process -Force
    if (-not $process.WaitForExit(5000)) {
        throw 'Release server did not exit during cleanup'
    }
}

function Assert-OrderedHumanMilestones([string]$text) {
    $expected = @(
        '[info] Starting Vector Network Analyzer server instrument_id=instrument-1',
        '[info] Factory preset loaded instrument_id=instrument-1',
        '[info] Continuous acquisition started instrument_id=instrument-1',
        '[info] Live display publication started instrument_id=instrument-1',
        '[info] Starting Web service instrument_id=instrument-1'
    )
    $previous = -1
    foreach ($milestone in $expected) {
        $position = $text.IndexOf(
            $milestone, $previous + 1, [StringComparison]::Ordinal)
        if ($position -lt 0) { throw "Missing human milestone: $milestone" }
        $previous = $position
    }
}

function Assert-OrderedJsonMilestones([object[]]$records) {
    $expected = @(
        @('server.lifecycle', 'starting'),
        @('server.factory_preset', 'loaded'),
        @('server.continuous_acquisition', 'running'),
        @('server.display_publication', 'running'),
        @('server.web_listener', 'starting')
    )
    $cursor = 0
    foreach ($milestone in $expected) {
        while ($cursor -lt $records.Count -and
               ($records[$cursor].event -ne $milestone[0] -or
                $records[$cursor].status -ne $milestone[1])) {
            $cursor++
        }
        if ($cursor -eq $records.Count) {
            throw "Missing JSON milestone: $($milestone -join '/')"
        }
        if ($records[$cursor].instrument_id -ne 'instrument-1') {
            throw "Milestone instrument ID is missing: $($milestone[0])"
        }
        $cursor++
    }
}

try {
    $start = Join-Path $release 'start.cmd'
    $launcher = Start-Process -FilePath 'cmd.exe' `
        -ArgumentList @('/d', '/c', ('"' + $start + '"')) `
        -WorkingDirectory $outside -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $health = Wait-ForHealth $launcher
    if (-not $health) {
        throw "Release health check failed: $(Read-Text $stderr)"
    }
    if ($health.StatusCode -ne 200) {
        throw "Release health returned HTTP $($health.StatusCode), expected 200"
    }

    $server = @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -eq $serverPath
    })
    if ($server.Count -ne 1) {
        throw "Expected one exact release server, found $($server.Count)"
    }
    $console = Read-Text $stdout
    if ($console -notmatch 'Starting Vector Network Analyzer' -or
        $console -notmatch 'Web URL: http://127\.0\.0\.1:8080/' -or
        $console -notmatch 'Text log: .*logs\\vna\.log' -or
        $console -notmatch 'Structured log: .*logs\\vna\.jsonl' -or
        $console -match '"event"\s*:|(?m)^\s*\{') {
        throw "Unexpected launcher output: $console"
    }
    Assert-OrderedHumanMilestones $console

    $logPath = Join-Path $release 'logs\vna.jsonl'
    $records = @(Get-Content -LiteralPath $logPath | ForEach-Object {
        $_ | ConvertFrom-Json
    })
    Assert-OrderedJsonMilestones $records

    # Acquisition runs continuously at about 10 Hz. Startup observability is
    # intentionally low-cardinality, so neither sink should grow per frame.
    $consoleLength = $console.Length
    $recordCount = $records.Count
    Start-Sleep -Milliseconds 1200
    if ((Read-Text $stdout).Length -ne $consoleLength -or
        @(Get-Content -LiteralPath $logPath).Count -ne $recordCount) {
        throw 'Startup sinks grew while only continuous frames were produced'
    }

    [pscustomobject]@{
        HealthStatus = $health.StatusCode
        ServerPid = $server[0].ProcessId
        Console = $console.Trim()
        LifecycleRecords = $records.Count
        LogPath = $logPath
        Artifacts = $artifacts
    } | Format-List
} finally {
    $cleanupFailure = $null
    try { Stop-ExactReleaseServer } catch { $cleanupFailure = $_ }
    if ($launcher -and -not $launcher.WaitForExit(5000)) {
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
    }
    if ($launcher -and $launcher.HasExited) {
        $errorText = Read-Text $stderr
        if ($launcher.ExitCode -eq 0 -or
            $errorText -notmatch 'ERROR: Vector Network Analyzer exited' -or
            $errorText -notmatch 'Text log:') {
            throw "Nonzero launcher guidance is missing: $errorText"
        }
    }
    if ($cleanupFailure) { throw $cleanupFailure }
}

$failureStdout = Join-Path $artifacts 'listen-failure-stdout.txt'
$failureStderr = Join-Path $artifacts 'listen-failure-stderr.txt'
$failureLauncher = $null
$portOwner = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 8080)
$portOwner.Server.ExclusiveAddressUse = $true
try {
    $portOwner.Start()
    $failureLauncher = Start-Process -FilePath 'cmd.exe' `
        -ArgumentList @('/d', '/c', ('"' + $start + '"')) `
        -WorkingDirectory $outside -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $failureStdout `
        -RedirectStandardError $failureStderr
    if (-not $failureLauncher.WaitForExit(15000)) {
        throw 'Release did not report listen failure within 15 seconds'
    }
    $failureLauncher.Refresh()
    if ($failureLauncher.ExitCode -eq 0) {
        throw 'Release reported success while port 8080 was occupied'
    }

    $failureConsole = Read-Text $failureStdout
    Assert-OrderedHumanMilestones $failureConsole
    if ($failureConsole -notmatch
        '\[error\] Web service failed to listen instrument_id=instrument-1' -or
        $failureConsole -match '"event"\s*:|(?m)^\s*\{') {
        throw "Listen failure console output is invalid: $failureConsole"
    }
    $failureError = Read-Text $failureStderr
    if ($failureError -notmatch 'ERROR: Vector Network Analyzer exited' -or
        $failureError -notmatch 'Text log:') {
        throw "Listen failure guidance is missing: $failureError"
    }
    $failureRecords = @(Get-Content -LiteralPath $logPath | ForEach-Object {
        $_ | ConvertFrom-Json
    })
    if (-not ($failureRecords | Where-Object {
        $_.event -eq 'server.web_listener' -and
        $_.status -eq 'listen_failed' -and
        $_.instrument_id -eq 'instrument-1'
    })) {
        throw 'Authoritative listen_failed event is missing from JSONL'
    }
    Write-Host "ListenFailureExitCode=$($failureLauncher.ExitCode)"
} finally {
    $cleanupFailure = $null
    try { Stop-ExactReleaseServer } catch { $cleanupFailure = $_ }
    if ($failureLauncher -and -not $failureLauncher.HasExited) {
        Stop-Process -Id $failureLauncher.Id -Force -ErrorAction SilentlyContinue
        if (-not $failureLauncher.WaitForExit(5000)) {
            $cleanupFailure = 'Listen-failure launcher did not stop within 5 seconds'
        }
    }
    $portOwner.Stop()
    if ($cleanupFailure) { throw $cleanupFailure }
}
