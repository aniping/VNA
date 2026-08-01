const defaultTraceColor = '#f2db24'

export function traceColorForMeasurement(measurementType?: string): string {
  // The display model has no color field yet; preserve the established palette
  // except for the manual-evidenced factory S21 Trace.
  return measurementType === 'S21' ? '#54d454' : defaultTraceColor
}
