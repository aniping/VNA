import type { MeasurementType } from './vnaApi.ts'

export type SParameter = MeasurementType

interface DisplayFrameCommon {
  readonly frameId: number
  readonly traceId: number
  readonly measurementId: number
  readonly measurementType: SParameter
  readonly generation: number
  readonly stateRevision: number
  readonly sequenceNumber: number
  readonly frequenciesHz: readonly number[]
}

export interface LogMagnitudeDisplayFrame extends DisplayFrameCommon {
  readonly format: 'logMagnitude'
  readonly valueUnit: 'dB'
  readonly values: readonly number[]
}

export interface PhaseDisplayFrame extends DisplayFrameCommon {
  readonly format: 'phase'
  readonly valueUnit: 'degree'
  readonly values: readonly number[]
}

export interface SmithDisplayFrame extends DisplayFrameCommon {
  readonly format: 'smith'
  readonly valueUnit: 'U'
  readonly values: readonly (readonly [number, number])[]
}

export type MultiFormatTraceDisplayFrame =
  | LogMagnitudeDisplayFrame
  | PhaseDisplayFrame
  | SmithDisplayFrame

export interface TraceDisplayFrameSet {
  readonly generation: number
  readonly sequenceNumber: number
  readonly frames: readonly MultiFormatTraceDisplayFrame[]
}

type JsonObject = Record<string, unknown>

function invalidSet(reason: string): never {
  throw new Error(`Invalid display frame set: ${reason}`)
}

function object(value: unknown, name: string): JsonObject {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    invalidSet(`${name} must be an object`)
  }
  return value as JsonObject
}

function integer(value: unknown, minimum: number, name: string): number {
  // JSON cannot preserve uint64 values beyond this boundary; rejecting them avoids aliased identities.
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < minimum) {
    invalidSet(`${name} must be an integer of at least ${minimum}`)
  }
  return value
}

function numbers(value: unknown, name: string): number[] {
  if (!Array.isArray(value)
    || !value.every((item) => typeof item === 'number' && Number.isFinite(item))) {
    invalidSet(`${name} must contain finite numbers`)
  }
  return value
}

function alignedPointCount(frequencies: readonly number[], values: readonly unknown[]): void {
  if (frequencies.length < 2 || frequencies.length > 2048 || values.length !== frequencies.length) {
    invalidSet('sample count must be aligned and between 2 and 2048')
  }
}

function frequencies(value: unknown): number[] {
  const result = numbers(value, 'frequencies')
  alignedPointCount(result, result)
  if (!result.every((item, index) => index === 0 || item > result[index - 1])) {
    invalidSet('frequencies must be strictly increasing')
  }
  return result
}

function parameter(value: unknown): SParameter {
  if (value !== 'S11' && value !== 'S21' && value !== 'S12' && value !== 'S22') {
    invalidSet('unsupported measurement type')
  }
  return value
}

function common(body: JsonObject): DisplayFrameCommon {
  return {
    frameId: integer(body.frameId, 1, 'frame id'),
    traceId: integer(body.traceId, 1, 'trace id'),
    measurementId: integer(body.measurementId, 1, 'measurement id'),
    measurementType: parameter(body.measurementType),
    generation: integer(body.generation, 1, 'generation'),
    stateRevision: integer(body.stateRevision, 0, 'state revision'),
    sequenceNumber: integer(body.sequenceNumber, 1, 'sequence number'),
    frequenciesHz: [...frequencies(body.frequenciesHz)],
  }
}

function pairs(value: unknown): [number, number][] {
  if (!Array.isArray(value) || !value.every((item) => Array.isArray(item)
    && item.length === 2 && item.every((part) => typeof part === 'number' && Number.isFinite(part)))) {
    invalidSet('Smith values must contain finite real and imaginary pairs')
  }
  return value.map((item) => [item[0], item[1]])
}

function scalarValues(value: unknown, frequenciesHz: readonly number[]): number[] {
  const result = numbers(value, 'values')
  alignedPointCount(frequenciesHz, result)
  return result
}

function smithValues(value: unknown, frequenciesHz: readonly number[]): [number, number][] {
  const result = pairs(value)
  alignedPointCount(frequenciesHz, result)
  return result
}

function decodeFrame(value: unknown): MultiFormatTraceDisplayFrame {
  const body = object(value, 'frame')
  const shared = common(body)
  if (body.format === 'logMagnitude' && body.valueUnit === 'dB') {
    return { ...shared, format: body.format, valueUnit: body.valueUnit,
      values: [...scalarValues(body.values, shared.frequenciesHz)] }
  }
  if (body.format === 'phase' && body.valueUnit === 'degree') {
    return { ...shared, format: body.format, valueUnit: body.valueUnit,
      values: [...scalarValues(body.values, shared.frequenciesHz)] }
  }
  if (body.format === 'smith' && body.valueUnit === 'U') {
    return { ...shared, format: body.format, valueUnit: body.valueUnit,
      values: smithValues(body.values, shared.frequenciesHz) }
  }
  return invalidSet('unsupported format or unit')
}

function sameNumbers(left: readonly number[], right: readonly number[]): boolean {
  return left.length === right.length && left.every((value, index) => value === right[index])
}

function validateSetIdentity(
  frames: readonly MultiFormatTraceDisplayFrame[], generation: number, sequenceNumber: number,
): void {
  const first = frames[0]
  const traceIds = new Set<number>()
  for (const frame of frames) {
    // A set is one acquisition atom: mixed epochs or axes would draw unrelated samples together.
    const sharedAcquisition = frame.generation === generation
      && frame.sequenceNumber === sequenceNumber && frame.frameId === first.frameId
      && frame.stateRevision === first.stateRevision
      && sameNumbers(frame.frequenciesHz, first.frequenciesHz)
    if (!sharedAcquisition || traceIds.has(frame.traceId)) {
      invalidSet('frames must share acquisition identity and have unique trace ids')
    }
    traceIds.add(frame.traceId)
  }
}

export function decodeTraceDisplayFrameSet(value: unknown): TraceDisplayFrameSet {
  const body = object(value, 'body')
  if (!Array.isArray(body.frames) || body.frames.length === 0) {
    invalidSet('frames must not be empty')
  }
  const generation = integer(body.generation, 1, 'generation')
  const sequenceNumber = integer(body.sequenceNumber, 1, 'sequence number')
  const frames = body.frames.map(decodeFrame)
  // Generation is checked only inside this message; a future session owns cross-message baselines.
  validateSetIdentity(frames, generation, sequenceNumber)
  return { generation, sequenceNumber, frames }
}
