export interface TraceDisplayFrame {
  readonly frameId: number
  readonly traceId: number
  // This is acquisition metadata; later display-state revisions do not invalidate dB samples.
  readonly stateRevision: number
  readonly sequenceNumber: number
  readonly format: 'logMagnitude'
  readonly valueUnit: 'dB'
  readonly frequenciesHz: readonly number[]
  readonly values: readonly number[]
}

type JsonObject = Record<string, unknown>

function invalidFrame(reason: string): never {
  throw new Error(`Invalid display frame response: ${reason}`)
}

function isObject(value: unknown): value is JsonObject {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function isSafeInteger(value: unknown, minimum: number): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= minimum
}

function isFiniteNumberArray(value: unknown): value is number[] {
  return Array.isArray(value)
    && value.every((item) => typeof item === 'number' && Number.isFinite(item))
}

function frequenciesIncrease(values: readonly number[]): boolean {
  return values.every((value, index) => index === 0 || value > values[index - 1])
}

export function decodeTraceDisplayFrame(body: unknown): TraceDisplayFrame {
  if (!isObject(body)) invalidFrame('body is not an object')
  if (!isSafeInteger(body.frameId, 1) || !isSafeInteger(body.traceId, 1)) {
    invalidFrame('invalid frame or trace identity')
  }
  if (!isSafeInteger(body.stateRevision, 0) || !isSafeInteger(body.sequenceNumber, 1)) {
    invalidFrame('invalid revision or sequence')
  }
  // Measurement conversion stays server-side; other formats or units must never reach this renderer.
  if (body.format !== 'logMagnitude' || body.valueUnit !== 'dB') {
    invalidFrame('unsupported format or value unit')
  }
  if (!isFiniteNumberArray(body.frequenciesHz) || !isFiniteNumberArray(body.values)) {
    invalidFrame('samples must be finite numeric arrays')
  }
  const points = body.frequenciesHz.length
  // Two points form the minimum frequency span; 2048 is the frozen display-frame contract limit.
  if (points < 2 || points > 2048 || body.values.length !== points) {
    invalidFrame('sample count must be aligned and between 2 and 2048')
  }
  if (!frequenciesIncrease(body.frequenciesHz)) {
    invalidFrame('frequencies must be strictly increasing')
  }
  return {
    frameId: body.frameId,
    traceId: body.traceId,
    stateRevision: body.stateRevision,
    sequenceNumber: body.sequenceNumber,
    format: body.format,
    valueUnit: body.valueUnit,
    frequenciesHz: [...body.frequenciesHz],
    values: [...body.values],
  }
}
