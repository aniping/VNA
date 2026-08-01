import type {
  CartesianScaleSnapshot, ChannelSnapshot, MeasurementSnapshot, StateSnapshot,
  SweepSettings, TraceSnapshot, WindowSnapshot,
} from './vnaApi.ts'

type JsonObject = Record<string, unknown>
function invalidState(reason: string): never {
  throw new Error(`Invalid state response: ${reason}`)
}

function object(value: unknown, name: string): JsonObject {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    invalidState(`${name} must be an object`)
  }
  return value as JsonObject
}

function array(value: unknown, name: string): unknown[] {
  if (!Array.isArray(value)) invalidState(`${name} must be an array`)
  return value
}

function integer(value: unknown, minimum: number, name: string, maximum = Number.MAX_SAFE_INTEGER): number {
  const valid = typeof value === 'number' && Number.isSafeInteger(value)
    && value >= minimum && value <= maximum
  if (!valid) {
    invalidState(`${name} must be an integer between ${minimum} and ${maximum}`)
  }
  return value
}

function finite(value: unknown, name: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value)) invalidState(`${name} must be finite`)
  return value
}

function decodeSweep(value: unknown): SweepSettings {
  const body = object(value, 'sweep')
  const sweep = {
    startFrequencyHz: integer(body.startFrequencyHz, 0, 'start frequency'),
    stopFrequencyHz: integer(body.stopFrequencyHz, 0, 'stop frequency'),
    points: integer(body.points, 2, 'points', 0xffff_ffff),
    ifBandwidthHz: integer(body.ifBandwidthHz, 1, 'IF bandwidth'),
    powerDbm: finite(body.powerDbm, 'power'),
  }
  if (sweep.startFrequencyHz >= sweep.stopFrequencyHz) invalidState('invalid sweep range')
  return sweep
}

function decodeChannel(value: unknown): ChannelSnapshot {
  const body = object(value, 'channel')
  if (body.sweepMode !== 'continuous' || body.triggerSource !== 'none') {
    invalidState('unsupported sweep mode or trigger source')
  }
  return {
    id: integer(body.id, 1, 'channel id'),
    sweep: decodeSweep(body.sweep),
    sweepMode: body.sweepMode,
    triggerSource: body.triggerSource,
  }
}

function decodeMeasurement(value: unknown): MeasurementSnapshot {
  const body = object(value, 'measurement')
  if (body.type !== 'S11' && body.type !== 'S21'
    && body.type !== 'S12' && body.type !== 'S22') invalidState('unsupported measurement type')
  return {
    id: integer(body.id, 1, 'measurement id'),
    channelId: integer(body.channelId, 1, 'measurement channel id'),
    type: body.type,
  }
}

function decodeWindow(value: unknown): WindowSnapshot {
  const body = object(value, 'window')
  return { id: integer(body.id, 1, 'window id') }
}

function decodeScale(value: unknown): CartesianScaleSnapshot | null {
  if (value === null) return null
  const body = object(value, 'scale')
  if (body.unit !== 'dB') invalidState('unsupported scale unit')
  const scale = {
    scalePerDivision: finite(body.scalePerDivision, 'scale per division'),
    referenceValue: finite(body.referenceValue, 'reference value'),
    referencePosition: finite(body.referencePosition, 'reference position'),
    minimum: finite(body.minimum, 'scale minimum'),
    maximum: finite(body.maximum, 'scale maximum'),
    unit: 'dB' as const,
  }
  if (scale.scalePerDivision <= 0 || scale.minimum >= scale.maximum) invalidState('invalid scale range')
  return scale
}

function decodeTrace(value: unknown): TraceSnapshot {
  const body = object(value, 'trace')
  if (body.format !== 'logMagnitude' && body.format !== 'phase' && body.format !== 'smith') {
    invalidState('unsupported trace format')
  }
  return {
    id: integer(body.id, 1, 'trace id'),
    windowId: integer(body.windowId, 1, 'trace window id'),
    measurementId: integer(body.measurementId, 1, 'trace measurement id'),
    format: body.format,
    scale: decodeScale(body.scale),
  }
}

export function decodeStateSnapshot(value: unknown): StateSnapshot {
  const body = object(value, 'body')
  const instrument = object(body.instrument, 'instrument')
  return {
    stateRevision: integer(body.stateRevision, 0, 'state revision'),
    instrument: {
      channels: array(instrument.channels, 'channels').map(decodeChannel),
      measurements: array(instrument.measurements, 'measurements').map(decodeMeasurement),
      windows: array(instrument.windows, 'windows').map(decodeWindow),
      traces: array(instrument.traces, 'traces').map(decodeTrace),
    },
  }
}
