import { decodeTraceDisplaySamples, type TraceDisplaySamples } from './traceDisplayFrameSet.ts'
export type SweepUserPhase = 'hold' | 'preparing' | 'sweeping' | 'calculation' | 'failed'
export interface SweepPreviewIdentity {
  readonly generation: number
  readonly sweepId: number
}
export interface SweepStreamStatus {
  readonly generation: number
  readonly channelId: number
  readonly stateRevision: number
  readonly sweepId: number | null
  readonly userPhase: SweepUserPhase
  readonly progress: {
    readonly completedAcquisitionPoints: number
    readonly totalAcquisitionPoints: number
  }
  readonly firstSweepAfterConfiguration: boolean
  readonly activePreviewIdentity: SweepPreviewIdentity | null
}
interface PreviewEventCommon {
  readonly eventCursor: number
  readonly sweepStatus: SweepStreamStatus
}
export interface SweepPreviewAvailable extends PreviewEventCommon, SweepPreviewIdentity {
  readonly type: 'available'
  readonly channelId: number
  readonly stateRevision: number
  readonly sequenceNumber: number
  readonly totalPointCount: number
  readonly traces: readonly TraceDisplaySamples[]
}
export interface SweepPreviewInvalidated extends PreviewEventCommon, SweepPreviewIdentity {
  readonly type: 'invalidated'
}
export interface SweepPreviewGenerationAdvanced extends PreviewEventCommon {
  readonly type: 'generationAdvanced'
  readonly generation: number
}
export interface SweepPreviewStatusChanged extends PreviewEventCommon {
  readonly type: 'status'
}
export type SweepPreviewEvent =
  | SweepPreviewAvailable
  | SweepPreviewInvalidated
  | SweepPreviewGenerationAdvanced
  | SweepPreviewStatusChanged
type JsonObject = Record<string, unknown>
function invalid(reason: string): never {
  throw new Error(`Invalid sweep preview event: ${reason}`)
}
function object(value: unknown, name: string): JsonObject {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    invalid(`${name} must be an object`)
  }
  return value as JsonObject
}
function integer(value: unknown, minimum: number, name: string): number {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < minimum) {
    invalid(`${name} must be an integer of at least ${minimum}`)
  }
  return value
}
function identity(value: unknown, name: string): SweepPreviewIdentity {
  const body = object(value, name)
  return {
    generation: integer(body.generation, 1, `${name} generation`),
    sweepId: integer(body.sweepId, 1, `${name} sweep id`),
  }
}
function userPhase(value: unknown): SweepUserPhase {
  if (value === 'hold' || value === 'preparing' || value === 'sweeping'
    || value === 'calculation' || value === 'failed') return value
  return invalid('unsupported user phase')
}
function progress(value: unknown): SweepStreamStatus['progress'] {
  const body = object(value, 'progress')
  const completedAcquisitionPoints = integer(body.completedAcquisitionPoints, 0, 'completed points')
  const totalAcquisitionPoints = integer(body.totalAcquisitionPoints, 0, 'total points')
  if (completedAcquisitionPoints > totalAcquisitionPoints) invalid('completed points exceed total')
  return { completedAcquisitionPoints, totalAcquisitionPoints }
}
function status(value: unknown): SweepStreamStatus {
  const body = object(value, 'sweep status')
  const generation = integer(body.generation, 1, 'status generation')
  const sweepId = body.sweepId === null ? null : integer(body.sweepId, 1, 'status sweep id')
  const active = body.activePreviewIdentity === null
    ? null : identity(body.activePreviewIdentity, 'active preview identity')
  if (active && (active.generation !== generation || active.sweepId !== sweepId)) {
    invalid('active preview identity must match status')
  }
  if (typeof body.firstSweepAfterConfiguration !== 'boolean') {
    invalid('first sweep marker must be boolean')
  }
  return {
    generation,
    channelId: integer(body.channelId, 1, 'status channel id'),
    stateRevision: integer(body.stateRevision, 0, 'status state revision'),
    sweepId,
    userPhase: userPhase(body.userPhase),
    progress: progress(body.progress),
    firstSweepAfterConfiguration: body.firstSweepAfterConfiguration,
    activePreviewIdentity: active,
  }
}
function eventCommon(body: JsonObject): PreviewEventCommon {
  return {
    eventCursor: integer(body.eventCursor, 1, 'event cursor'),
    sweepStatus: status(body.sweepStatus),
  }
}
function available(body: JsonObject): SweepPreviewAvailable {
  if (!Array.isArray(body.traces) || body.traces.length === 0) invalid('traces must not be empty')
  const common = eventCommon(body)
  const generation = integer(body.generation, 1, 'generation')
  const sweepId = integer(body.sweepId, 1, 'sweep id')
  const channelId = integer(body.channelId, 1, 'channel id')
  const stateRevision = integer(body.stateRevision, 0, 'state revision')
  const totalPointCount = integer(body.totalPointCount, 2, 'total point count')
  if (totalPointCount > 2048) invalid('total point count exceeds 2048')
  const traces = body.traces.map((trace) => decodeTraceDisplaySamples(trace, 1))
  const unique = new Set(traces.map(({ traceId }) => traceId)).size === traces.length
  const withinTotal = traces.every(({ frequenciesHz }) => frequenciesHz.length <= totalPointCount)
  const statusMatches = common.sweepStatus.generation === generation
    && common.sweepStatus.sweepId === sweepId
    && common.sweepStatus.channelId === channelId
    && common.sweepStatus.stateRevision === stateRevision
  if (!unique || !withinTotal || !statusMatches) invalid('available event identity is inconsistent')
  return { type: 'available', ...common, generation, sweepId, channelId, stateRevision,
    sequenceNumber: integer(body.sequenceNumber, 1, 'sequence number'), totalPointCount, traces }
}
export function decodeSweepPreviewEvent(value: unknown): SweepPreviewEvent {
  const body = object(value, 'event')
  if (body.type === 'available') return available(body)
  const common = eventCommon(body)
  if (body.type === 'invalidated') {
    const generation = integer(body.generation, 1, 'generation')
    const sweepId = integer(body.sweepId, 1, 'sweep id')
    if (common.sweepStatus.generation !== generation
      || common.sweepStatus.activePreviewIdentity !== null) {
      invalid('invalidation status mismatch')
    }
    return { type: body.type, ...common, generation, sweepId }
  }
  if (body.type === 'generationAdvanced') {
    const generation = integer(body.generation, 1, 'generation')
    if (common.sweepStatus.generation !== generation) invalid('generation status mismatch')
    return { type: body.type, ...common, generation }
  }
  if (body.type === 'status') return { type: body.type, ...common }
  return invalid('unsupported event type')
}
