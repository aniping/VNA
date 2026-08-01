export interface SmithComplexPoint {
  readonly real: number
  readonly imaginary: number
}

export interface SmithPlotPoint {
  readonly x: number
  readonly y: number
}

export function projectSmithPoints(
  samples: readonly SmithComplexPoint[],
): readonly SmithPlotPoint[] {
  // The backend owns Sij and all domain conversions. SVG only reverses the vertical axis;
  // zero is normalized to +0 so path and test consumers share one representation of the origin.
  return samples.map(({ real, imaginary }) => ({
    x: real,
    y: imaginary === 0 ? 0 : -imaginary,
  }))
}
