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
