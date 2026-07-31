<script setup lang="ts">
import { computed } from 'vue'
import type { StateSnapshot } from '../api/vnaApi'
import type { TraceDisplayFrame } from '../api/traceDisplayFrameApi'
import DiagramPane from './DiagramPane.vue'

const props = defineProps<{
  state: StateSnapshot | null
  activeTraceId?: number
  maximized: boolean
  frames?: ReadonlyMap<number, TraceDisplayFrame>
}>()
const emit = defineEmits<{ selectTrace: [traceId: number] }>()
const kinds = ['smith', 'cartesian', 'cartesian', 'smith'] as const
const channel = computed(() => props.state?.instrument.channels[0])

function traceForPane(index: number) {
  return props.state?.instrument.traces[index]
}

function measurementForPane(index: number) {
  const trace = traceForPane(index)
  return props.state?.instrument.measurements.find((item) => item.id === trace?.measurementId)
}

function frameForPane(index: number): TraceDisplayFrame | undefined {
  const trace = traceForPane(index)
  return trace ? props.frames?.get(trace.id) : undefined
}

function kindForPane(index: number): 'cartesian' | 'smith' {
  const trace = traceForPane(index)
  if (trace) return trace.format === 'smith' ? 'smith' : 'cartesian'
  return kinds[index]
}
</script>

<template>
  <section class="diagram-grid" :class="{ maximized }" aria-label="Measurement diagrams">
    <DiagramPane
      v-for="paneNumber in 4"
      :key="paneNumber"
      :class="{
        'pane-hidden': maximized && traceForPane(paneNumber - 1)?.id !== activeTraceId,
      }"
      :pane-number="paneNumber"
      :kind="kindForPane(paneNumber - 1)"
      :channel="channel"
      :trace="traceForPane(paneNumber - 1)"
      :frame="frameForPane(paneNumber - 1)"
      :measurement="measurementForPane(paneNumber - 1)"
      :active="activeTraceId !== undefined && traceForPane(paneNumber - 1)?.id === activeTraceId"
      @select="emit('selectTrace', $event)"
    />
  </section>
</template>

<style scoped>
.diagram-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); grid-template-rows: repeat(2, minmax(0, 1fr)); min-width: 0; min-height: 0; background: #050707; }
.diagram-grid.maximized { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
.pane-hidden { display: none; }
</style>
