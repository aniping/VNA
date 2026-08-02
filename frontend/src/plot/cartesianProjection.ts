export interface CartesianSamples {
  readonly frequenciesHz: readonly number[]
  readonly values: readonly number[]
}

export interface CartesianSegmentedSamples {
  readonly frequencyMinimumHz: number
  readonly frequencyMaximumHz: number
  readonly segments: readonly CartesianSamples[]
}

export interface CartesianAxisRange {
  readonly minimum: number
  readonly maximum: number
}

export interface NormalizedPlotPoint {
  readonly x: number
  readonly y: number
}

function projectSegment(
  samples: CartesianSamples,
  range: CartesianAxisRange,
  frequencyMinimumHz: number,
  frequencySpan: number,
): readonly NormalizedPlotPoint[] {
  const valueSpan = range.maximum - range.minimum
  if (frequencySpan <= 0 || valueSpan <= 0
    || samples.frequenciesHz.length !== samples.values.length) return []

  // Values are already formatted by the backend; projection only maps display truth to pixels.
  return samples.frequenciesHz.map((frequency, index) => ({
    x: (frequency - frequencyMinimumHz) / frequencySpan,
    // Out-of-range values stay outside 0..1 so crossing segments clip at the SVG viewport.
    y: (range.maximum - samples.values[index]) / valueSpan,
  }))
}

export function projectCartesianSegments(
  samples: CartesianSegmentedSamples,
  range: CartesianAxisRange,
): readonly (readonly NormalizedPlotPoint[])[] {
  const frequencySpan = samples.frequencyMaximumHz - samples.frequencyMinimumHz
  return samples.segments.map((segment) => projectSegment(
    segment,
    range,
    samples.frequencyMinimumHz,
    frequencySpan,
  ))
}

export function projectCartesianPoints(
  samples: CartesianSamples,
  range: CartesianAxisRange,
): readonly NormalizedPlotPoint[] {
  const frequencyMinimumHz = samples.frequenciesHz[0]
  const frequencyMaximumHz = samples.frequenciesHz[samples.frequenciesHz.length - 1]
  return projectSegment(
    samples,
    range,
    frequencyMinimumHz,
    frequencyMaximumHz - frequencyMinimumHz,
  )
}
