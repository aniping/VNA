import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  StateSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'

export interface DisplayDiagram {
  windowId: number
  trace?: TraceSnapshot
  measurement?: MeasurementSnapshot
  channel?: ChannelSnapshot
}

export function selectDisplayDiagrams(
  state: StateSnapshot | null,
  activeTraceId?: number,
): DisplayDiagram[] {
  if (!state) return []
  const instrument = state.instrument

  return instrument.windows.map((window) => {
    const windowTraces = instrument.traces.filter((trace) => trace.windowId === window.id)
    // A Window is the Diagram boundary. The current pane renders one Trace header,
    // so prefer its active Trace without turning that UI choice into domain ownership.
    const trace = windowTraces.find((candidate) => candidate.id === activeTraceId)
      ?? windowTraces[0]
    const measurement = instrument.measurements.find((item) => item.id === trace?.measurementId)
    const channel = instrument.channels.find((item) => item.id === measurement?.channelId)
    return { windowId: window.id, trace, measurement, channel }
  })
}
