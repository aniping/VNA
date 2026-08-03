import type { StateSnapshot } from './vnaApi.ts'
import type {
  MultiFormatTraceDisplayFrame, TraceDisplayFrameSet, TraceDisplaySamples,
} from './traceDisplayFrameSet.ts'
import type {
  SweepPreviewAvailable, SweepPreviewEvent, SweepStreamStatus,
} from './sweepPreview.ts'

function belongsToSnapshot(
  frame: TraceDisplaySamples,
  snapshot: StateSnapshot,
): boolean {
  const trace = snapshot.instrument.traces.find(({ id }) => id === frame.traceId)
  if (!trace || trace.measurementId !== frame.measurementId || trace.format !== frame.format) {
    return false
  }
  const measurement = snapshot.instrument.measurements.find(({ id }) => id === frame.measurementId)
  return measurement?.type === frame.measurementType
}
export type DisplayFrameSetMap = ReadonlyMap<number, MultiFormatTraceDisplayFrame>
export interface SweepProjectionAxis {
  readonly frequencyMinimumHz: number
  readonly frequencyMaximumHz: number
}
export interface CurrentSweepPartial {
  readonly generation: number
  readonly sweepId: number
  readonly stateRevision: number
  readonly totalPointCount: number
  readonly traces: ReadonlyMap<number, SweepPreviewAvailable['traces'][number]>
  readonly axis: SweepProjectionAxis | null
}
export interface LiveDisplayState {
  readonly generation: number
  readonly lastComplete: DisplayFrameSetMap
  readonly currentPartial: CurrentSweepPartial | null
  readonly sweepStatus: SweepStreamStatus | null
}
export function emptyLiveDisplayState(): LiveDisplayState {
  return { generation: 0, lastComplete: new Map(), currentPartial: null, sweepStatus: null }
}
export function filterDisplayFrameSetForSnapshot(
  frameSet: TraceDisplayFrameSet,
  snapshot: StateSnapshot,
): TraceDisplayFrameSet {
  // Trace identity spans display and measurement state. Checking every facet prevents a late
  // frame from being drawn under a reused Trace id after configuration changes.
  const frames = frameSet.frames.filter((frame) => belongsToSnapshot(frame, snapshot))
  // Generation belongs to the future live-session baseline, not snapshot compatibility.
  return frames.length === frameSet.frames.length ? frameSet : { ...frameSet, frames }
}

export function replaceDisplayFramesForSnapshot(
  frameSet: TraceDisplayFrameSet,
  snapshot: StateSnapshot,
): DisplayFrameSetMap {
  const compatible = filterDisplayFrameSetForSnapshot(frameSet, snapshot)
  // A set is one acquisition atom. Rebuilding the map ensures a missing current Trace cannot
  // silently retain samples from an older measurement, format, or generation.
  return new Map(compatible.frames.map((frame) => [frame.traceId, frame]))
}

export function replaceCompleteDisplayFramesForSnapshot(
  frameSet: TraceDisplayFrameSet,
  snapshot: StateSnapshot,
  minimumStateRevision: number,
): DisplayFrameSetMap | null {
  if (snapshot.stateRevision < minimumStateRevision) return null
  if (frameSet.frames.some((frame) => frame.stateRevision !== snapshot.stateRevision)) return null
  const next = replaceDisplayFramesForSnapshot(frameSet, snapshot)
  // All-S reconfiguration changes the whole publication plan. Requiring every authoritative
  // Trace and no extras rejects both partial and mixed-generation publications as one atom.
  const exactSize = next.size === snapshot.instrument.traces.length
    && next.size === frameSet.frames.length
  return exactSize ? next : null
}

export function retainDisplayFramesForSnapshot(
  current: DisplayFrameSetMap,
  snapshot: StateSnapshot,
): DisplayFrameSetMap {
  const retained = [...current.values()].filter((frame) => belongsToSnapshot(frame, snapshot))
  if (retained.length === current.size) return current
  return new Map(retained.map((frame) => [frame.traceId, frame]))
}

export function removeDisplayFrame(
  current: DisplayFrameSetMap,
  traceId: number,
): DisplayFrameSetMap {
  if (!current.has(traceId)) return current
  const next = new Map(current)
  next.delete(traceId)
  return next
}
function previewBelongsToSnapshot(
  preview: SweepPreviewAvailable['traces'][number],
  channelId: number,
  snapshot: StateSnapshot,
): boolean {
  if (!belongsToSnapshot(preview, snapshot)) return false
  const measurement = snapshot.instrument.measurements.find(({ id }) => id === preview.measurementId)
  return measurement?.channelId === channelId
}
function projectionAxis(
  event: SweepPreviewAvailable,
  snapshot: StateSnapshot,
  current: LiveDisplayState,
): SweepProjectionAxis | null | false {
  const channel = snapshot.instrument.channels.find(({ id }) => id === event.channelId)
  const configured = snapshot.sweepRuntime.configured
  if (!channel) return false
  if (event.stateRevision === configured.stateRevision) {
    return channel.sweep.points === event.totalPointCount ? {
      frequencyMinimumHz: channel.sweep.startFrequencyHz,
      frequencyMaximumHz: channel.sweep.stopFrequencyHz,
    } : false
  }
  const prior = current.currentPartial
  const sameSweep = prior?.generation === event.generation && prior.sweepId === event.sweepId
    && prior.stateRevision === event.stateRevision && prior.totalPointCount === event.totalPointCount
  if (sameSweep) return prior.axis
  const completeMatches = event.traces.every((trace) => {
    const frame = current.lastComplete.get(trace.traceId)
    return frame?.generation === event.generation && frame.stateRevision === event.stateRevision
      && frame.frequenciesHz.length === event.totalPointCount
      && trace.frequenciesHz.every((frequency, index) => frequency === frame.frequenciesHz[index])
  })
  return completeMatches ? null : false
}

function currentPartial(
  event: SweepPreviewAvailable,
  snapshot: StateSnapshot,
  current: LiveDisplayState,
): CurrentSweepPartial | null {
  const runtime = snapshot.sweepRuntime
  const compatibleRevision = event.stateRevision === runtime.configured.stateRevision
    || event.stateRevision === runtime.applied.stateRevision
  if (!compatibleRevision) return null
  const traces = event.traces.filter((trace) => (
    previewBelongsToSnapshot(trace, event.channelId, snapshot)
  ))
  if (traces.length !== event.traces.length) return null
  const axis = projectionAxis(event, snapshot, current)
  if (axis === false) return null
  return {
    generation: event.generation,
    sweepId: event.sweepId,
    stateRevision: event.stateRevision,
    totalPointCount: event.totalPointCount,
    traces: new Map(traces.map((trace) => [trace.traceId, trace])),
    axis,
  }
}

function identityMatchesPartial(
  partial: CurrentSweepPartial,
  identity: SweepStreamStatus['activePreviewIdentity'],
): boolean {
  return identity?.generation === partial.generation && identity.sweepId === partial.sweepId
}

export function acceptCompleteFrameSet(
  current: LiveDisplayState,
  frameSet: TraceDisplayFrameSet,
  snapshot: StateSnapshot,
): LiveDisplayState {
  if (frameSet.generation < current.generation) return current
  const generationChanged = frameSet.generation > current.generation
  return {
    generation: frameSet.generation,
    lastComplete: replaceDisplayFramesForSnapshot(frameSet, snapshot),
    currentPartial: generationChanged ? null : current.currentPartial,
    sweepStatus: generationChanged ? null : current.sweepStatus,
  }
}

export function acceptSweepPreviewEvent(
  current: LiveDisplayState,
  event: SweepPreviewEvent,
  snapshot: StateSnapshot,
): LiveDisplayState {
  const eventGeneration = event.type === 'status'
    ? event.sweepStatus.generation : event.generation
  if (eventGeneration < current.generation) return current
  const advanced = eventGeneration > current.generation
  const base = advanced
    ? { ...current, generation: eventGeneration, lastComplete: new Map(), currentPartial: null }
    : current
  if (event.type === 'available') {
    const partial = currentPartial(event, snapshot, current)
    return partial ? { ...base, currentPartial: partial, sweepStatus: event.sweepStatus } : current
  }
  if (event.type === 'generationAdvanced') {
    return { ...base, generation: event.generation, lastComplete: new Map(),
      currentPartial: null, sweepStatus: event.sweepStatus }
  }
  if (event.type === 'invalidated') {
    const exact = base.currentPartial?.generation === event.generation
      && base.currentPartial.sweepId === event.sweepId
    return { ...base, currentPartial: exact ? null : base.currentPartial,
      sweepStatus: event.sweepStatus }
  }
  const keepPartial = base.currentPartial
    && identityMatchesPartial(base.currentPartial, event.sweepStatus.activePreviewIdentity)
  return { ...base, currentPartial: keepPartial ? base.currentPartial : null,
    sweepStatus: event.sweepStatus }
}

export function retainLiveDisplayForSnapshot(
  current: LiveDisplayState,
  snapshot: StateSnapshot,
): LiveDisplayState {
  const generation = snapshot.sweepRuntime.applied.generation
  const sameGeneration = current.generation === generation
  const lastComplete = sameGeneration
    ? retainDisplayFramesForSnapshot(current.lastComplete, snapshot) : new Map()
  const partial = sameGeneration ? current.currentPartial : null
  const revisionCompatible = partial
    && (partial.stateRevision === snapshot.sweepRuntime.configured.stateRevision
      || partial.stateRevision === snapshot.sweepRuntime.applied.stateRevision)
  const traces = revisionCompatible ? [...partial.traces.values()].filter((trace) => {
    const channelId = snapshot.instrument.measurements.find(
      ({ id }) => id === trace.measurementId,
    )?.channelId
    return channelId !== undefined && previewBelongsToSnapshot(trace, channelId, snapshot)
  }) : []
  const currentPartial = partial && traces.length > 0
    ? { ...partial, traces: new Map(traces.map((trace) => [trace.traceId, trace])) }
    : null
  return { ...current, generation, lastComplete, currentPartial,
    sweepStatus: sameGeneration ? current.sweepStatus : null }
}

export function clearLiveDisplayData(current: LiveDisplayState): LiveDisplayState {
  return { ...current, lastComplete: new Map(), currentPartial: null }
}

export function removeLiveDisplayTrace(
  current: LiveDisplayState,
  traceId: number,
): LiveDisplayState {
  const lastComplete = removeDisplayFrame(current.lastComplete, traceId)
  if (!current.currentPartial?.traces.has(traceId)) return { ...current, lastComplete }
  const traces = new Map(current.currentPartial.traces)
  traces.delete(traceId)
  const currentPartial = traces.size ? { ...current.currentPartial, traces } : null
  return { ...current, lastComplete, currentPartial }
}
