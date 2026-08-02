export interface SvgPathPoint {
  readonly x: number
  readonly y: number
}

function segmentPath(points: readonly SvgPathPoint[]): string {
  return points
    .map((point, index) => `${index === 0 ? 'M' : 'L'} ${point.x} ${point.y}`)
    .join(' ')
}

export function segmentedSvgPath(
  segments: readonly (readonly SvgPathPoint[])[],
): string {
  // Every block starts with M so SVG never invents a line through frequencies
  // that the acquisition has not supplied.
  return segments.map(segmentPath).filter(Boolean).join(' ')
}
