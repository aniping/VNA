import type { TraceDisplayFrame } from '../api/traceDisplayFrameApi'
import type { CartesianScaleSnapshot } from '../api/vnaApi'

export interface NormalizedPlotPoint {
  readonly x: number
  readonly y: number
}

export function projectLogMagnitudePoints(
  frame: TraceDisplayFrame,
  scale: CartesianScaleSnapshot,
): readonly NormalizedPlotPoint[] {
  const firstFrequency = frame.frequenciesHz[0]
  const lastFrequency = frame.frequenciesHz[frame.frequenciesHz.length - 1]
  const frequencySpan = lastFrequency - firstFrequency
  const valueSpan = scale.maximum - scale.minimum
  if (frequencySpan <= 0 || valueSpan <= 0 || frame.frequenciesHz.length !== frame.values.length) {
    return []
  }

  // Samples are already dB. This module only projects backend-owned frequency and Scale truth.
  return frame.frequenciesHz.map((frequency, index) => ({
    x: (frequency - firstFrequency) / frequencySpan,
    // Values intentionally remain outside 0..1 so the SVG viewport clips crossing segments.
    y: (scale.maximum - frame.values[index]) / valueSpan,
  }))
}
