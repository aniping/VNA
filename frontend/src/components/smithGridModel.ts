export const normalizedResistanceCircleValues = [0.2, 0.5, 1, 2, 5] as const
export const normalizedResistanceLabelValues = [0, 0.2, 0.5, 1, 2, 5, 10, Infinity] as const
export const normalizedReactanceMagnitudes = [0.2, 0.5, 1, 2, 5] as const
export const normalizedMagnitudeRadii = [0.2, 0.4, 0.6, 0.8] as const

// For normalized impedance z=r+jx, Γ=(z-1)/(z+1). Holding r or x constant
// yields the circles below; their unit-circle intersections also locate the labels.

export interface SmithGridCircle {
  readonly centerX: number
  readonly centerY: number
  readonly radius: number
}

export interface SmithGridPoint {
  readonly x: number
  readonly y: number
}

export function resistanceCircle(resistance: number): SmithGridCircle {
  const denominator = 1 + resistance
  return { centerX: resistance / denominator, centerY: 0, radius: 1 / denominator }
}

export function reactanceCircle(reactance: number): SmithGridCircle {
  // ZNB's Smith grid is static normalized-impedance geometry. This never converts Trace samples.
  return { centerX: 1, centerY: -1 / reactance, radius: 1 / Math.abs(reactance) }
}

export function resistanceLabelPoint(resistance: number): SmithGridPoint {
  if (!Number.isFinite(resistance)) return { x: 1, y: 0 }
  return { x: (resistance - 1) / (resistance + 1), y: 0 }
}

export function reactanceLabelPoint(reactance: number): SmithGridPoint {
  const denominator = reactance * reactance + 1
  return {
    x: (reactance * reactance - 1) / denominator,
    y: -2 * reactance / denominator,
  }
}

export function formatNormalizedLabel(value: number): string {
  if (value === Infinity) return '∞'
  return `${value}`.replace(/^(-?)0\./, '$1.')
}
