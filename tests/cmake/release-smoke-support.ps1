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

function Assert-OrderedHumanMilestones([string]$text) {
    $expected = @(
        '[info] Starting Vector Network Analyzer server instrument_id=instrument-1',
        '[info] Factory preset loaded: Channel 1, S21, Trace 1, 201 points, 10 MHz–26.5 GHz instrument_id=instrument-1',
        '[info] Continuous acquisition started: 100 ms, ports 1/2, IFBW 10 kHz, power -10 dBm instrument_id=instrument-1',
        '[info] Live display publication started: Trace 1, Log Magnitude instrument_id=instrument-1',
        '[info] Starting Web service at http://127.0.0.1:8080/ instrument_id=instrument-1'
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
                $records[$cursor].status -ne $milestone[1])) { $cursor++ }
        if ($cursor -eq $records.Count) {
            throw "Missing JSON milestone: $($milestone -join '/')"
        }
        if ($records[$cursor].instrument_id -ne 'instrument-1') {
            throw "Milestone instrument ID is missing: $($milestone[0])"
        }
        $cursor++
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

function Assert-WebCommandLogs(
    [string]$human,
    [object[]]$records) {
    $lines = @($human -split '\r?\n')
    $acceptedHuman = @($lines | Where-Object {
        $_ -match '\[info\] Create channel succeeded ' -and
        $_ -match 'command_id=release-accepted(?: |$)' -and
        $_ -match 'state_revision=1(?: |$)'
    })
    $rejectedHuman = @($lines | Where-Object {
        $_ -match '\[warning\] Create channel rejected ' -and
        $_ -match 'command_id=release-rejected(?: |$)' -and
        $_ -match 'state_revision=1(?: |$)' -and
        $_ -match 'error_code=state-revision-conflict(?: |$)'
    })
    if ($acceptedHuman.Count -ne 1 -or $rejectedHuman.Count -ne 1) {
        throw 'Human command audit entries are missing or duplicated'
    }
    $accepted = @($records | Where-Object {
        $_.event -eq 'web.command.create_channel' -and
        $_.command_id -eq 'release-accepted' -and $_.status -eq 'succeeded' -and
        $_.message -eq 'Create channel succeeded' -and
        $_.state_revision -eq 1 -and
        -not $_.PSObject.Properties['error_code']
    })
    $rejected = @($records | Where-Object {
        $_.event -eq 'web.command.create_channel' -and
        $_.command_id -eq 'release-rejected' -and $_.status -eq 'rejected' -and
        $_.message -eq 'Create channel rejected' -and
        $_.state_revision -eq 1 -and
        $_.error_code -eq 'state-revision-conflict'
    })
    if ($accepted.Count -ne 1 -or $rejected.Count -ne 1) {
        throw 'Structured command audit entries are missing or duplicated'
    }
}
