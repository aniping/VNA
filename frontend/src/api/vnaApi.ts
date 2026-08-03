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
  sweepMode: SweepMode
  sweepCount: number
  triggerSource: 'none'
}

export type SweepMode = 'continuous' | 'single'

export interface SweepExecutionSnapshot {
  stateRevision: number
  mode: SweepMode
  sweepCount: number
}

export interface AppliedSweepExecutionSnapshot extends SweepExecutionSnapshot {
  generation: number
}

export interface SweepRuntimeSnapshot {
  state: 'running' | 'stopped' | 'retired' | 'failed'
  phase: 'hold' | 'preparing' | 'sweeping' | 'calculation' | 'failed'
  configured: SweepExecutionSnapshot
  applied: AppliedSweepExecutionSnapshot
}

export interface MeasurementSnapshot {
  id: number
  channelId: number
  type: MeasurementType
}

export interface WindowSnapshot {
  id: number
}

export interface CartesianScaleSnapshot {
  scalePerDivision: number
  referenceValue: number
  referencePosition: number
  minimum: number
  maximum: number
  unit: 'dB'
}

export interface TraceSnapshot {
  id: number
  windowId: number
  measurementId: number
  format: TraceFormat
  scale: CartesianScaleSnapshot | null
}

export type MeasurementType = 'S11' | 'S21' | 'S12' | 'S22'
export type CreatableMeasurementType = 'S11' | 'S21'
export type TraceFormat = 'logMagnitude' | 'phase' | 'smith'

export interface TraceSetup {
  measurementType: CreatableMeasurementType
  format: TraceFormat
}

export interface StateSnapshot {
  stateRevision: number
  sweepRuntime: SweepRuntimeSnapshot
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
  return decodeStateSnapshot(await readJson<unknown>(await fetch('/api/v1/state')))
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
  type: CreatableMeasurementType,
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

export async function updateChannelSweepControl(
  stateRevision: number,
  channelId: number,
  mode: SweepMode,
  sweepCount: number,
): Promise<CommandResult<{ channelId: number }>> {
  return sendCommand(stateRevision, 'updateChannelSweepControl', { channelId, mode, sweepCount })
}

export async function setTraceMeasurementType(
  stateRevision: number,
  traceId: number,
  measurementType: MeasurementType,
): Promise<CommandResult<{ traceId: number }>> {
  return sendCommand(stateRevision, 'setTraceMeasurementType', { traceId, measurementType })
}

export async function ensureAllSParameters(
  stateRevision: number,
  traceId: number,
): Promise<CommandResult<unknown>> {
  return sendCommand(stateRevision, 'ensureAllSParameters', { traceId })
}

export async function updateTraceScalePerDivision(
  stateRevision: number,
  traceId: number,
  value: number,
): Promise<CommandResult<{ traceId: number }>> {
  return sendCommand(stateRevision, 'updateTraceScalePerDivision', {
    traceId,
    scalePerDivision: value,
  })
}

export async function startSingleSweep(
  stateRevision: number,
  channelId: number,
  signal?: AbortSignal,
): Promise<CommandResult<{ operationId: number }>> {
  const result = await sendCommand<{ operationId: number }>(
    stateRevision,
    'startSingleSweep',
    { channelId },
    signal,
  )
  if (!Number.isSafeInteger(result.value?.operationId) || result.value.operationId < 1) {
    throw new Error('Invalid start sweep response')
  }
  return result
}

function createCommandId(): string {
  // randomUUID is unavailable on plain-HTTP LAN origins. getRandomValues is
  // deliberately exposed there and still gives commands collision-safe IDs.
  const bytes = globalThis.crypto.getRandomValues(new Uint8Array(16))
  bytes[6] = (bytes[6] & 0x0f) | 0x40
  bytes[8] = (bytes[8] & 0x3f) | 0x80
  const hex = Array.from(bytes, (value) => value.toString(16).padStart(2, '0')).join('')
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`
}

async function sendCommand<T>(
  stateRevision: number,
  type: string,
  payload: unknown,
  signal?: AbortSignal,
): Promise<CommandResult<T>> {
  const response = await fetch('/api/v1/commands', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    signal,
    body: JSON.stringify({
      commandId: createCommandId(),
      sessionId: 'browser-session',
      instrumentId: 'instrument-1',
      expectedStateRevision: stateRevision,
      type,
      payload,
    }),
  })
  return readJson<CommandResult<T>>(response)
}
import { decodeStateSnapshot } from './stateSnapshotDecoder.ts'
