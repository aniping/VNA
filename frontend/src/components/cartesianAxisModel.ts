import type { CartesianAxisRange } from '../plot/cartesianProjection.ts'
import type { TraceSnapshot } from '../api/vnaApi.ts'

export interface CartesianAxisModel {
  readonly range: CartesianAxisRange
  readonly unit: 'dB' | 'degree'
  readonly scalePerDivision: number
  readonly referenceValue: number
  readonly referencePosition: number
}

export const phaseAxisRange = { minimum: -225, maximum: 225 } as const

// The backend wraps samples at ±180°, while ZNB deliberately shows one extra division
// above and below that domain; the viewport therefore must not be inferred from samples.
const phaseAxis: CartesianAxisModel = {
  range: phaseAxisRange,
  unit: 'degree',
  scalePerDivision: 45,
  referenceValue: 0,
  referencePosition: 5,
}

export function selectCartesianAxis(trace?: TraceSnapshot): CartesianAxisModel | null {
  if (trace?.format === 'phase') return phaseAxis
  if (trace?.format !== 'logMagnitude' || !trace.scale) return null
  const scale = trace.scale
  // Tick projection consumes authoritative boundaries; it never reconstructs Scale coupling.
  return {
    range: { minimum: scale.minimum, maximum: scale.maximum },
    unit: scale.unit,
    scalePerDivision: scale.scalePerDivision,
    referenceValue: scale.referenceValue,
    referencePosition: scale.referencePosition,
  }
}

export function cartesianAxisTicks(axis: CartesianAxisModel): readonly number[] {
  const span = axis.range.maximum - axis.range.minimum
  return Array.from({ length: 11 }, (_, index) => axis.range.maximum - span * index / 10)
}

function formatAxisValue(value: number, unit: CartesianAxisModel['unit']): string {
  const normalized = Math.abs(value) < 1e-9 ? 0 : value
  const text = Number.isInteger(normalized) ? normalized.toFixed(0) : `${normalized}`
  return unit === 'degree' ? `${text}°` : `${text} dB`
}

export function formatCartesianAxisTick(
  value: number,
  unit: CartesianAxisModel['unit'],
): string {
  return formatAxisValue(value, unit)
}

export function formatCartesianScaleSummary(axis: CartesianAxisModel): string {
  return `${formatAxisValue(axis.scalePerDivision, axis.unit)}/ Ref ${formatAxisValue(axis.referenceValue, axis.unit)}`
}
