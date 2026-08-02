param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseRoot
)

. (Join-Path $PSScriptRoot 'release-smoke-support.ps1')

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
$logsPath = Join-Path $release 'logs'

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
        $console -match 'Text log:|Structured log:') {
        throw "Unexpected launcher output: $console"
    }
    if (Test-Path -LiteralPath $logsPath) {
        throw 'Release created the removed logs directory'
    }

    $accepted = Invoke-CreateChannelCommand 'release-accepted' 0
    $rejected = Invoke-CreateChannelCommand 'release-rejected' 0
    if ($accepted.StatusCode -ne 200 -or $rejected.StatusCode -ne 409 -or
        ($rejected.Body | ConvertFrom-Json).errorCode -ne
            'state-revision-conflict') {
        throw 'Release command responses do not match the HTTP fixture'
    }
    if (Test-Path -LiteralPath $logsPath) {
        throw 'Web commands recreated the removed logs directory'
    }

    [pscustomobject]@{
        HealthStatus = $health.StatusCode
        ServerPid = $server[0].ProcessId
        Console = $console.Trim()
        Artifacts = $artifacts
    } | Format-List
} finally {
    $cleanupFailure = $null
    try { Stop-ExactReleaseServer $serverPath } catch { $cleanupFailure = $_ }
    if ($launcher -and -not $launcher.WaitForExit(5000)) {
        Stop-Process -Id $launcher.Id -Force -ErrorAction SilentlyContinue
    }
    if ($launcher -and $launcher.HasExited) {
        $errorText = Read-Text $stderr
        if ($launcher.ExitCode -eq 0 -or
            $errorText -notmatch 'ERROR: Vector Network Analyzer exited') {
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
    if ($failureConsole -notmatch 'Starting Vector Network Analyzer' -or
        $failureConsole -notmatch 'Web URL: http://127\.0\.0\.1:8080/' -or
        $failureConsole -match 'Text log:|Structured log:') {
        throw "Listen failure console output is invalid: $failureConsole"
    }
    $failureError = Read-Text $failureStderr
    if ($failureError -notmatch 'ERROR: Vector Network Analyzer exited') {
        throw "Listen failure guidance is missing: $failureError"
    }
    if (Test-Path -LiteralPath $logsPath) {
        throw 'Listen failure created the removed logs directory'
    }
    Write-Host "ListenFailureExitCode=$($failureLauncher.ExitCode)"
} finally {
    $cleanupFailure = $null
    try { Stop-ExactReleaseServer $serverPath } catch { $cleanupFailure = $_ }
    if ($failureLauncher -and -not $failureLauncher.HasExited) {
        Stop-Process -Id $failureLauncher.Id -Force -ErrorAction SilentlyContinue
        if (-not $failureLauncher.WaitForExit(5000)) {
            $cleanupFailure = 'Listen-failure launcher did not stop within 5 seconds'
        }
    }
    $portOwner.Stop()
    if ($cleanupFailure) { throw $cleanupFailure }
}
