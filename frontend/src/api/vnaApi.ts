export interface SweepSettings {
  startFrequencyHz: number
  stopFrequencyHz: number
  points: number
  ifBandwidthHz: number
  powerDbm: number
}

export interface ChannelSnapshot {
  id: number
  sweep: SweepSettings
}

export interface MeasurementSnapshot {
  id: number
  channelId: number
  type: string
}

export interface WindowSnapshot {
  id: number
}

export interface TraceSnapshot {
  id: number
  windowId: number
  measurementId: number
  format: string
}

export interface StateSnapshot {
  stateRevision: number
  instrument: {
    channels: ChannelSnapshot[]
    measurements: MeasurementSnapshot[]
    windows: WindowSnapshot[]
    traces: TraceSnapshot[]
  }
}

interface HealthResponse {
  status: string
}

interface CommandResult {
  status: string
  stateRevision: number
}

async function readJson<T>(response: Response): Promise<T> {
  if (!response.ok) {
    throw new Error(`HTTP ${response.status}`)
  }
  return response.json() as Promise<T>
}

export async function checkHealth(): Promise<void> {
  const health = await readJson<HealthResponse>(await fetch('/api/v1/health'))
  if (health.status !== 'ok') {
    throw new Error('Service is not ready')
  }
}

export async function fetchState(): Promise<StateSnapshot> {
  return readJson<StateSnapshot>(await fetch('/api/v1/state'))
}

export async function createChannel(
  stateRevision: number,
  sweep: SweepSettings,
): Promise<CommandResult> {
  const response = await fetch('/api/v1/commands', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      commandId: crypto.randomUUID(),
      sessionId: 'browser-session',
      instrumentId: 'instrument-1',
      expectedStateRevision: stateRevision,
      type: 'createChannel',
      payload: sweep,
    }),
  })
  return readJson<CommandResult>(response)
}
