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

    $server = @(Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -eq $serverPath
    })
    if ($server.Count -ne 1) {
        throw "Expected one exact release server, found $($server.Count)"
    }
    $console = Read-Text $stdout
    if ($console -notmatch 'Starting Vector Network Analyzer' -or
        $console -notmatch 'Web URL: http://127\.0\.0\.1:8080/' -or
        $console -match '"event"|server\.lifecycle') {
        throw "Unexpected launcher output: $console"
    }

    $logPath = Join-Path $release 'logs\vna.log.jsonl'
    $records = @(Get-Content -LiteralPath $logPath | ForEach-Object {
        $_ | ConvertFrom-Json
    })
    if (-not ($records | Where-Object {
        $_.event -eq 'server.lifecycle' -and $_.status -eq 'starting'
    })) {
        throw 'Authoritative lifecycle event is missing from the JSONL file'
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
    if (-not $server) {
        $server = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ExecutablePath -eq $serverPath })
    }
    if ($server -and $server.Count -eq 1) {
        Stop-Process -Id $server[0].ProcessId -Force -ErrorAction SilentlyContinue
    }
    if ($launcher -and -not $launcher.WaitForExit(5000)) {
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
    }
    if ($launcher -and $launcher.HasExited) {
        $errorText = Read-Text $stderr
        if ($launcher.ExitCode -eq 0 -or
            $errorText -notmatch 'ERROR: Vector Network Analyzer exited' -or
            $errorText -notmatch 'Log file:') {
            throw "Nonzero launcher guidance is missing: $errorText"
        }
    }
}
