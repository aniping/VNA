import { computed, ref, watch, type Ref } from 'vue'
import type { StateSnapshot } from '../api/vnaApi.ts'

export function useActiveTrace(state: Readonly<Ref<StateSnapshot | null>>) {
  // The id is navigation-only UI state. Trace and Measurement content always comes from
  // the latest snapshot so a successful command never creates an optimistic business copy.
  const activeTraceId = ref<number>()
  const activeTrace = computed(() => {
    const traces = state.value?.instrument.traces ?? []
    return traces.find((trace) => trace.id === activeTraceId.value) ?? traces[0]
  })
  const activeMeasurement = computed(() => state.value?.instrument.measurements
    .find((measurement) => measurement.id === activeTrace.value?.measurementId))

  watch(activeTrace, (trace) => {
    activeTraceId.value = trace?.id
  }, { immediate: true })

  return { activeTraceId, activeTrace, activeMeasurement }
}
