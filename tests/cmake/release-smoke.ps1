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
$logFile = Join-Path $logsPath 'vna.log'

function Require-OrderedText([string]$text, [string[]]$expected) {
    $position = 0
    foreach ($value in $expected) {
        $next = $text.IndexOf($value, $position)
        if ($next -lt 0) { throw "Missing ordered text '$value'" }
        $position = $next + $value.Length
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
        $console -notmatch 'Log file:.*logs\\vna\.log') {
        throw "Unexpected launcher output: $console"
    }
    $milestones = @(
        '[服务启动] 矢量网络分析仪服务正在启动',
        '[工厂预置] 已加载：通道 1，S21，Trace 1，频率 10 MHz 至 26.5 GHz，201 点，IFBW 10 kHz，功率 -10 dBm',
        '[连续扫频] 仿真持续测量已启动',
        '[服务启动] Web 服务准备监听：0.0.0.0:8080；本机访问：http://127.0.0.1:8080/'
    )
    Require-OrderedText $console $milestones
    $logText = Read-Text $logFile
    Require-OrderedText $logText $milestones
    if (Get-ChildItem -LiteralPath $logsPath -Filter '*.jsonl') {
        throw 'Release unexpectedly created JSON Lines output'
    }

    $accepted = Invoke-CreateChannelCommand 'release-accepted' 0
    $rejected = Invoke-CreateChannelCommand 'release-rejected' 0
    if ($accepted.StatusCode -ne 200 -or $rejected.StatusCode -ne 409 -or
        ($rejected.Body | ConvertFrom-Json).errorCode -ne
            'state-revision-conflict') {
        throw 'Release command responses do not match the HTTP fixture'
    }
    $businessLog = Wait-ForText $logFile 'error_code=state-revision-conflict'
    Require-OrderedText $businessLog @(
        '[配置命令] 创建通道请求已成功处理 | command_id=release-accepted | session_id=release-smoke | instrument_id=instrument-1 | revision=1 | channel_id=2',
        '[配置命令] 创建通道请求被拒绝 | command_id=release-rejected | session_id=release-smoke | instrument_id=instrument-1 | revision=1 | error_code=state-revision-conflict'
    )
    $continuousLog = Wait-ForText $logFile `
        '[连续扫频] 已发布配置代次首个完整显示帧 | generation=1 | revision=0'
    $firstFrame = [regex]::Match(
        $continuousLog,
        'generation=1 \| revision=0 \| frame_id=([1-9][0-9]*) \| sweep_id=([1-9][0-9]*) \| sequence=([1-9][0-9]*) \| trace_count=1')
    if (-not $firstFrame.Success) {
        throw 'Continuous first-frame log did not contain real nonzero IDs'
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

$fallbackRelease = Join-Path $artifacts 'fallback-release'
New-Item -ItemType Directory -Path $fallbackRelease | Out-Null
Copy-Item -LiteralPath (Join-Path $release 'bin') `
    -Destination $fallbackRelease -Recurse
Copy-Item -LiteralPath (Join-Path $release 'web') `
    -Destination $fallbackRelease -Recurse
New-Item -ItemType File -Path (Join-Path $fallbackRelease 'logs') | Out-Null
$fallbackServer = Join-Path $fallbackRelease 'bin\vna-server.exe'
$fallbackStdout = Join-Path $artifacts 'fallback-stdout.txt'
$fallbackStderr = Join-Path $artifacts 'fallback-stderr.txt'
$fallbackProcess = $null
try {
    $fallbackProcess = Start-Process -FilePath $fallbackServer `
        -WorkingDirectory $outside -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $fallbackStdout `
        -RedirectStandardError $fallbackStderr
    $fallbackHealth = Wait-ForHealth $fallbackProcess
    if (-not $fallbackHealth -or $fallbackHealth.StatusCode -ne 200) {
        throw 'Console-only fallback server did not become healthy'
    }
    $fallbackConsole = Read-Text $fallbackStdout
    $fallbackError = Read-Text $fallbackStderr
    if ($fallbackConsole -notmatch '无法写入日志文件，将仅输出到控制台' -or
        $fallbackConsole -notmatch '\[服务启动\] Web 服务准备监听') {
        throw 'Console-only fallback did not preserve runtime messages'
    }
    if ($fallbackError -notmatch 'Runtime log file is unavailable') {
        throw 'Console-only fallback did not report the file failure'
    }
    Write-Host 'ConsoleOnlyFallbackHealth=200'
} finally {
    Stop-ExactReleaseServer $fallbackServer
}

$exceptionRelease = Join-Path $artifacts 'exception-release'
New-Item -ItemType Directory -Path $exceptionRelease | Out-Null
Copy-Item -LiteralPath (Join-Path $release 'bin') `
    -Destination $exceptionRelease -Recurse
$exceptionServer = Join-Path $exceptionRelease 'bin\vna-server.exe'
$exceptionStdout = Join-Path $artifacts 'exception-stdout.txt'
$exceptionStderr = Join-Path $artifacts 'exception-stderr.txt'
$exceptionProcess = $null
try {
    $exceptionProcess = Start-Process -FilePath $exceptionServer `
        -WorkingDirectory $outside -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $exceptionStdout `
        -RedirectStandardError $exceptionStderr
    if (-not $exceptionProcess.WaitForExit(15000)) {
        throw 'Startup exception did not terminate within 15 seconds'
    }
    $exceptionProcess.Refresh()
    if ($exceptionProcess.ExitCode -ne 1) {
        throw "Startup exception returned $($exceptionProcess.ExitCode), expected 1"
    }
    $exceptionLog = Join-Path $exceptionRelease 'logs\vna.log'
    $failurePattern = 'ERROR    \[服务启动\] 服务器启动失败：'
    if ((Read-Text $exceptionStdout) -notmatch $failurePattern -or
        (Read-Text $exceptionLog) -notmatch $failurePattern) {
        throw 'Unhandled startup exception was not flushed to both sinks'
    }
    Write-Host "StartupExceptionExitCode=$($exceptionProcess.ExitCode)"
} finally {
    Stop-ExactReleaseServer $exceptionServer
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
        $failureConsole -notmatch 'Log file:.*logs\\vna\.log') {
        throw "Listen failure console output is invalid: $failureConsole"
    }
    $failureError = Read-Text $failureStderr
    if ($failureError -notmatch 'ERROR: Vector Network Analyzer exited') {
        throw "Listen failure guidance is missing: $failureError"
    }
    if ((Read-Text $logFile) -notmatch
        '\[服务启动\] Web 服务监听失败：http://127\.0\.0\.1:8080/') {
        throw 'Listen failure was not written to vna.log'
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
