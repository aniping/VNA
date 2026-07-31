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

export type MeasurementType = 'S11' | 'S21'
export type TraceFormat = 'logMagnitude' | 'phase' | 'smith'

export interface TraceSetup {
  measurementType: MeasurementType
  format: TraceFormat
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

interface CommandResult<T> {
  status: string
  stateRevision: number
  value: T
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
): Promise<CommandResult<{ channelId: number }>> {
  return sendCommand(stateRevision, 'createChannel', sweep)
}

export async function updateChannelSweep(
  stateRevision: number,
  channelId: number,
  sweep: SweepSettings,
): Promise<CommandResult<{ channelId: number }>> {
  return sendCommand(stateRevision, 'updateChannelSweep', { channelId, ...sweep })
}

export async function createMeasurement(
  stateRevision: number,
  channelId: number,
  type: MeasurementType,
): Promise<CommandResult<{ measurementId: number }>> {
  return sendCommand(stateRevision, 'createMeasurement', { channelId, type })
}

export async function createWindow(
  stateRevision: number,
): Promise<CommandResult<{ windowId: number }>> {
  return sendCommand(stateRevision, 'createWindow', {})
}

export async function createTrace(
  stateRevision: number,
  windowId: number,
  measurementId: number,
  format: TraceFormat,
): Promise<CommandResult<{ traceId: number }>> {
  return sendCommand(stateRevision, 'createTrace', { windowId, measurementId, format })
}

export async function updateTraceFormat(
  stateRevision: number,
  traceId: number,
  format: TraceFormat,
): Promise<CommandResult<{ traceId: number }>> {
  return sendCommand(stateRevision, 'updateTraceFormat', { traceId, format })
}

async function sendCommand<T>(
  stateRevision: number,
  type: string,
  payload: unknown,
): Promise<CommandResult<T>> {
  const response = await fetch('/api/v1/commands', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      commandId: crypto.randomUUID(),
      sessionId: 'browser-session',
      instrumentId: 'instrument-1',
      expectedStateRevision: stateRevision,
      type,
      payload,
    }),
  })
  return readJson<CommandResult<T>>(response)
}
