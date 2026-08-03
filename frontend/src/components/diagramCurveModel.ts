import type { MultiFormatTraceDisplayFrame } from '../api/traceDisplayFrameSet.ts'
import type { CurrentSweepPartial } from '../api/displayFrameSetState.ts'
import type { TraceDisplaySamples } from '../api/traceDisplayFrameSet.ts'
import type { MeasurementSnapshot, TraceSnapshot } from '../api/vnaApi.ts'
import type {
  CartesianAxisRange,
  CartesianSegmentedSamples,
} from '../plot/cartesianProjection.ts'
import type { SmithComplexPoint } from '../plot/smithProjection.ts'
import { phaseAxisRange } from './cartesianAxisModel.ts'

interface CartesianCurveModel {
  readonly kind: 'cartesian'
  readonly traceId: number
  readonly label: 'Log Magnitude' | 'Phase'
  readonly unit: 'dB' | 'degree'
  readonly samples: CartesianSegmentedSamples
  readonly range: CartesianAxisRange
}

interface SmithCurveModel {
  readonly kind: 'smith'
  readonly traceId: number
  readonly segments: readonly (readonly SmithComplexPoint[])[]
}

export type DiagramCurveModel = CartesianCurveModel | SmithCurveModel

function identityMatches(
  trace: TraceSnapshot,
  measurement: MeasurementSnapshot,
  frame: TraceDisplaySamples,
): boolean {
  return frame.traceId === trace.id
    && frame.measurementId === trace.measurementId
    && frame.measurementId === measurement.id
    && frame.measurementType === measurement.type
    && frame.format === trace.format
}

function segmentedCartesianSamples(
  series: readonly Extract<TraceDisplaySamples, { format: 'logMagnitude' | 'phase' }>[],
  frequencyMinimumHz: number,
  frequencyMaximumHz: number,
): CartesianSegmentedSamples {
  return {
    frequencyMinimumHz,
    frequencyMaximumHz,
    segments: series.map(({ frequenciesHz, values }) => ({ frequenciesHz, values })),
  }
}

function previewForTrace(
  partial: CurrentSweepPartial | undefined,
  trace: TraceSnapshot,
  measurement: MeasurementSnapshot,
): TraceDisplaySamples | undefined {
  const preview = partial?.traces.get(trace.id)
  return preview && identityMatches(trace, measurement, preview) ? preview : undefined
}

function prefixMatches(
  complete: MultiFormatTraceDisplayFrame,
  preview: TraceDisplaySamples,
  totalPointCount: number,
): boolean {
  return complete.frequenciesHz.length === totalPointCount
    && preview.frequenciesHz.every((value, index) => value === complete.frequenciesHz[index])
}

function cartesianSeries(
  complete: Extract<MultiFormatTraceDisplayFrame, { format: 'logMagnitude' | 'phase' }> | undefined,
  preview: Extract<TraceDisplaySamples, { format: 'logMagnitude' | 'phase' }> | undefined,
  partial: CurrentSweepPartial | undefined,
): CartesianSegmentedSamples | null {
  const compatible = complete && preview && partial?.generation === complete.generation
    && prefixMatches(complete, preview, partial.totalPointCount)
  if (compatible) {
    return segmentedCartesianSamples([complete, preview], complete.frequenciesHz[0],
      complete.frequenciesHz[complete.frequenciesHz.length - 1])
  }
  if (preview && partial?.axis) {
    return segmentedCartesianSamples([preview], partial.axis.frequencyMinimumHz,
      partial.axis.frequencyMaximumHz)
  }
  return complete ? segmentedCartesianSamples([complete], complete.frequenciesHz[0],
    complete.frequenciesHz[complete.frequenciesHz.length - 1]) : null
}

export function selectDiagramCurve(
  trace?: TraceSnapshot,
  measurement?: MeasurementSnapshot,
  frame?: MultiFormatTraceDisplayFrame,
  partial?: CurrentSweepPartial,
): DiagramCurveModel | null {
  if (!trace || !measurement) return null
  const complete = frame && identityMatches(trace, measurement, frame) ? frame : undefined
  const preview = previewForTrace(partial, trace, measurement)
  if (trace.format === 'logMagnitude' && trace.scale?.unit === 'dB') {
    const samples = cartesianSeries(
      complete?.format === 'logMagnitude' ? complete : undefined,
      preview?.format === 'logMagnitude' ? preview : undefined,
      partial,
    )
    if (!samples) return null
    return { kind: 'cartesian', traceId: trace.id, label: 'Log Magnitude', unit: 'dB',
      samples,
      range: { minimum: trace.scale.minimum, maximum: trace.scale.maximum } }
  }
  if (trace.format === 'phase') {
    const samples = cartesianSeries(
      complete?.format === 'phase' ? complete : undefined,
      preview?.format === 'phase' ? preview : undefined,
      partial,
    )
    if (!samples) return null
    // The backend owns wrapping and degrees; this fixed viewport only projects its frozen domain.
    return { kind: 'cartesian', traceId: trace.id, label: 'Phase', unit: 'degree',
      samples, range: phaseAxisRange }
  }
  if (trace.format === 'smith') {
    const smithComplete = complete?.format === 'smith' ? complete : undefined
    const smithPreview = preview?.format === 'smith' ? preview : undefined
    const compatible = smithComplete && smithPreview && partial?.generation === smithComplete.generation
      && prefixMatches(smithComplete, smithPreview, partial.totalPointCount)
    const series = compatible ? [smithComplete, smithPreview]
      : smithPreview ? [smithPreview] : smithComplete ? [smithComplete] : []
    if (series.length === 0) return null
    // Pair-to-point conversion changes representation only; no RF-domain math lives here.
    const segments = series.map(({ values }) => (
      values.map(([real, imaginary]) => ({ real, imaginary }))
    ))
    return { kind: 'smith', traceId: trace.id, segments }
  }
  return null
}
