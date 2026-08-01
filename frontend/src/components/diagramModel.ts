import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  StateSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'

export interface DisplayDiagram {
  windowId: number
  active: boolean
  trace?: TraceSnapshot
  measurement?: MeasurementSnapshot
  channel?: ChannelSnapshot
}

export function traceDisplayEmptyMessage(format?: TraceSnapshot['format']): string {
  return format && format !== 'logMagnitude'
    ? 'Display data unavailable for current format'
    : 'No measurement data'
}

export function selectDisplayDiagrams(
  state: StateSnapshot | null,
  activeTraceId?: number,
): DisplayDiagram[] {
  if (!state) return []
  const instrument = state.instrument
  // Before any user selection, the first real Trace establishes the manual's default active pane.
  const selectedTraceId = activeTraceId ?? instrument.traces[0]?.id

  return instrument.windows.map((window) => {
    const windowTraces = instrument.traces.filter((trace) => trace.windowId === window.id)
    // A Window is the Diagram boundary. The current pane renders one Trace header,
    // so prefer its active Trace without turning that UI choice into domain ownership.
    const trace = windowTraces.find((candidate) => candidate.id === selectedTraceId)
      ?? windowTraces[0]
    const measurement = instrument.measurements.find((item) => item.id === trace?.measurementId)
    const channel = instrument.channels.find((item) => item.id === measurement?.channelId)
    return { windowId: window.id, active: trace?.id === selectedTraceId, trace, measurement, channel }
  })
}
