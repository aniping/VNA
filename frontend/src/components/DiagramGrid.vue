<script setup lang="ts">
import { computed } from 'vue'
import type { StateSnapshot } from '../api/vnaApi'
import DiagramPane from './DiagramPane.vue'

const props = defineProps<{ state: StateSnapshot | null }>()
const kinds = ['smith', 'cartesian', 'cartesian', 'smith'] as const
const channel = computed(() => props.state?.instrument.channels[0])

function traceForPane(index: number) {
  return props.state?.instrument.traces[index]
}

function measurementForPane(index: number) {
  const trace = traceForPane(index)
  return props.state?.instrument.measurements.find((item) => item.id === trace?.measurementId)
}
</script>

<template>
  <section class="diagram-grid" aria-label="Measurement diagrams">
    <DiagramPane
      v-for="(kind, index) in kinds"
      :key="index"
      :pane-number="index + 1"
      :kind="kind"
      :channel="channel"
      :trace="traceForPane(index)"
      :measurement="measurementForPane(index)"
    />
  </section>
</template>

<style scoped>
.diagram-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); grid-template-rows: repeat(2, minmax(0, 1fr)); min-width: 0; min-height: 0; background: #050707; }
</style>
