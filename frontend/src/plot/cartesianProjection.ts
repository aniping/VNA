export interface CartesianSamples {
  readonly frequenciesHz: readonly number[]
  readonly values: readonly number[]
}

export interface CartesianAxisRange {
  readonly minimum: number
  readonly maximum: number
}

export interface NormalizedPlotPoint {
  readonly x: number
  readonly y: number
}

export function projectCartesianPoints(
  samples: CartesianSamples,
  range: CartesianAxisRange,
): readonly NormalizedPlotPoint[] {
  const firstFrequency = samples.frequenciesHz[0]
  const lastFrequency = samples.frequenciesHz[samples.frequenciesHz.length - 1]
  const frequencySpan = lastFrequency - firstFrequency
  const valueSpan = range.maximum - range.minimum
  if (frequencySpan <= 0 || valueSpan <= 0
    || samples.frequenciesHz.length !== samples.values.length) return []

  // Values are already formatted by the backend; projection only maps display truth to pixels.
  return samples.frequenciesHz.map((frequency, index) => ({
    x: (frequency - firstFrequency) / frequencySpan,
    // Out-of-range values stay outside 0..1 so crossing segments clip at the SVG viewport.
    y: (range.maximum - samples.values[index]) / valueSpan,
  }))
}
