import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  StateSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'
import { sParameters } from './measurementSofttoolModel.ts'

export interface DisplayDiagram {
  windowId: number
  active: boolean
  trace?: TraceSnapshot
  measurement?: MeasurementSnapshot
  channel?: ChannelSnapshot
}

export const noMeasurementDataMessage = 'No measurement data'

function allSParameterRank(diagram: DisplayDiagram): number {
  return diagram.measurement ? sParameters.indexOf(diagram.measurement.type) : -1
}

function orderAllSParameterDiagrams(
  diagrams: DisplayDiagram[],
  traces: readonly TraceSnapshot[],
): DisplayDiagram[] {
  if (diagrams.length !== sParameters.length) return diagrams
  const channelId = diagrams[0]?.channel?.id
  if (channelId === undefined || diagrams.some((diagram) => diagram.channel?.id !== channelId)) {
    return diagrams
  }
  if (diagrams.some((diagram) => (
    traces.filter((trace) => trace.windowId === diagram.windowId).length !== 1
  ))) return diagrams
  const ranks = diagrams.map(allSParameterRank)
  if (ranks.includes(-1)) return diagrams
  if (new Set(ranks).size !== sParameters.length) return diagrams
  // Only the exact two-port quartet gets the manual's matrix. Other Window orders remain authority-owned.
  return [...diagrams].sort((left, right) => allSParameterRank(left) - allSParameterRank(right))
}

export function selectDisplayDiagrams(
  state: StateSnapshot | null,
  activeTraceId?: number,
): DisplayDiagram[] {
  if (!state) return []
  const instrument = state.instrument
  // Before any user selection, the first real Trace establishes the manual's default active pane.
  const selectedTraceId = activeTraceId ?? instrument.traces[0]?.id

  const diagrams = instrument.windows.map((window) => {
    const windowTraces = instrument.traces.filter((trace) => trace.windowId === window.id)
    // A Window is the Diagram boundary. The current pane renders one Trace header,
    // so prefer its active Trace without turning that UI choice into domain ownership.
    const trace = windowTraces.find((candidate) => candidate.id === selectedTraceId)
      ?? windowTraces[0]
    const measurement = instrument.measurements.find((item) => item.id === trace?.measurementId)
    const channel = instrument.channels.find((item) => item.id === measurement?.channelId)
    return { windowId: window.id, active: trace?.id === selectedTraceId, trace, measurement, channel }
  })
  return orderAllSParameterDiagrams(diagrams, instrument.traces)
}
