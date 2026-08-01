import { decodeTraceDisplayFrame } from './traceDisplayFrame'
import type { TraceDisplayFrame } from './traceDisplayFrame'

export type { TraceDisplayFrame } from './traceDisplayFrame'

export async function fetchTraceDisplayFrame(
  traceId: number,
  signal?: AbortSignal,
): Promise<TraceDisplayFrame | null> {
  const response = await fetch(`/api/v1/traces/${traceId}/display-frame`, {
    cache: 'no-store',
    signal,
  })
  if (response.status === 204) return null
  if (response.status !== 200) throw new Error(`HTTP ${response.status}`)
  return decodeTraceDisplayFrame(await response.json())
}
