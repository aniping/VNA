const defaultTraceColor = '#f2db24'
const traceColors = ['#54d454', defaultTraceColor, '#36c5d8', '#d879e8'] as const

export function traceColorForTrace(traceId?: number): string {
  // Trace id is the stable identity available in the snapshot. The deterministic project palette
  // survives Measurement changes and refreshes without inventing a backend color property.
  return traceId ? traceColors[(traceId - 1) % traceColors.length] : defaultTraceColor
}
