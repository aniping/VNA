import type { StateSnapshot } from './vnaApi.ts'
import type {
  MultiFormatTraceDisplayFrame, TraceDisplayFrameSet,
} from './traceDisplayFrameSet.ts'

function belongsToSnapshot(
  frame: MultiFormatTraceDisplayFrame,
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
