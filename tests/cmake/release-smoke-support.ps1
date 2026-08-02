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
        return Get-Content -LiteralPath $path -Raw -Encoding UTF8
    }
    return ''
}

function Wait-ForText([string]$path, [string]$needle) {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        $text = Read-Text $path
        if ($text.Contains($needle)) { return $text }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for '$needle' in $path"
}

function Stop-ExactReleaseServer([string]$expectedPath) {
    $candidates = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -eq $expectedPath })
    if ($candidates.Count -gt 1) {
        throw "Refusing to stop $($candidates.Count) release server processes"
    }
    if ($candidates.Count -eq 0) { return }
    $processId = $candidates[0].ProcessId
    $confirmed = Get-CimInstance Win32_Process `
        -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue
    if (-not $confirmed -or $confirmed.ExecutablePath -ne $expectedPath) {
        throw 'Release server PID changed before cleanup'
    }
    $process = Get-Process -Id $processId -ErrorAction Stop
    Stop-Process -InputObject $process -Force
    if (-not $process.WaitForExit(5000)) {
        throw 'Release server did not exit during cleanup'
    }
}

function Invoke-CreateChannelCommand(
    [string]$commandId,
    [uint64]$expectedRevision) {
    $request = @{
        commandId = $commandId
        sessionId = 'release-smoke'
        instrumentId = 'instrument-1'
        expectedStateRevision = $expectedRevision
        type = 'createChannel'
        payload = @{
            startFrequencyHz = 10000000
            stopFrequencyHz = 26500000000
            points = 201
            ifBandwidthHz = 10000
            powerDbm = -10.0
        }
    } | ConvertTo-Json -Depth 4 -Compress
    $client = [Net.Http.HttpClient]::new()
    try {
        $content = [Net.Http.StringContent]::new(
            $request, [Text.Encoding]::UTF8, 'application/json')
        $response = $client.PostAsync(
            'http://127.0.0.1:8080/api/v1/commands', $content
        ).GetAwaiter().GetResult()
        return [pscustomobject]@{
            StatusCode = [int]$response.StatusCode
            Body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        }
    } finally {
        $client.Dispose()
    }
}
