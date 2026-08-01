import type { TraceDisplayFrame } from './traceDisplayFrame.ts'
import type { TraceSnapshot } from './vnaApi.ts'

export type DisplayFrameMap = ReadonlyMap<number, TraceDisplayFrame>

export function createLegacyFrameGuard() {
  const blockedTraceIds = new Set<number>()
  return {
    block: (traceId: number): void => { blockedTraceIds.add(traceId) },
    accepts: (frame: Pick<TraceDisplayFrame, 'traceId'>): boolean => (
      !blockedTraceIds.has(frame.traceId)
    ),
  }
}

export function removeDisplayFrame(
  current: DisplayFrameMap,
  traceId: number,
): DisplayFrameMap {
  if (!current.has(traceId)) return current
  const next = new Map(current)
  next.delete(traceId)
  return next
}

export function replaceLatestDisplayFrame(
  current: DisplayFrameMap,
  frame: TraceDisplayFrame,
): DisplayFrameMap {
  // Vue observes the Map reference. Copy-on-write also prevents a later frame
  // from making previously rendered state appear to change retroactively.
  const next = new Map(current)
  next.set(frame.traceId, frame)
  return next
}

export function retainDisplayableFrames(
  current: DisplayFrameMap,
  traces: readonly Pick<TraceSnapshot, 'id' | 'format'>[],
): DisplayFrameMap {
  const validTraceIds = new Set(
    traces.filter((trace) => trace.format === 'logMagnitude').map((trace) => trace.id),
  )
  const retained = new Map(
    [...current].filter(([traceId]) => validTraceIds.has(traceId)),
  )
  return retained.size === current.size ? current : retained
}
