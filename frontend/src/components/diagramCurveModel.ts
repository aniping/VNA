import type { MultiFormatTraceDisplayFrame } from '../api/traceDisplayFrameSet.ts'
import type { MeasurementSnapshot, TraceSnapshot } from '../api/vnaApi.ts'
import type { CartesianAxisRange, CartesianSamples } from '../plot/cartesianProjection.ts'
import type { SmithComplexPoint } from '../plot/smithProjection.ts'
import { phaseAxisRange } from './cartesianAxisModel.ts'

interface CartesianCurveModel {
  readonly kind: 'cartesian'
  readonly traceId: number
  readonly label: 'Log Magnitude' | 'Phase'
  readonly unit: 'dB' | 'degree'
  readonly samples: CartesianSamples
  readonly range: CartesianAxisRange
}

interface SmithCurveModel {
  readonly kind: 'smith'
  readonly traceId: number
  readonly samples: readonly SmithComplexPoint[]
}

export type DiagramCurveModel = CartesianCurveModel | SmithCurveModel

function identityMatches(
  trace: TraceSnapshot,
  measurement: MeasurementSnapshot,
  frame: MultiFormatTraceDisplayFrame,
): boolean {
  return frame.traceId === trace.id
    && frame.measurementId === trace.measurementId
    && frame.measurementId === measurement.id
    && frame.measurementType === measurement.type
    && frame.format === trace.format
}

export function selectDiagramCurve(
  trace?: TraceSnapshot,
  measurement?: MeasurementSnapshot,
  frame?: MultiFormatTraceDisplayFrame,
): DiagramCurveModel | null {
  if (!trace || !measurement || !frame || !identityMatches(trace, measurement, frame)) return null
  if (frame.format === 'logMagnitude' && trace.scale?.unit === 'dB') {
    return { kind: 'cartesian', traceId: trace.id, label: 'Log Magnitude', unit: 'dB',
      samples: { frequenciesHz: frame.frequenciesHz, values: frame.values },
      range: { minimum: trace.scale.minimum, maximum: trace.scale.maximum } }
  }
  if (frame.format === 'phase') {
    // The backend owns wrapping and degrees; this fixed viewport only projects its frozen domain.
    return { kind: 'cartesian', traceId: trace.id, label: 'Phase', unit: 'degree',
      samples: { frequenciesHz: frame.frequenciesHz, values: frame.values }, range: phaseAxisRange }
  }
  if (frame.format === 'smith') {
    // Pair-to-point conversion changes representation only; no RF-domain math lives here.
    const samples = frame.values.map(([real, imaginary]) => ({ real, imaginary }))
    return { kind: 'smith', traceId: trace.id, samples }
  }
  return null
}
