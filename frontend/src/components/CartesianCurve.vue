<script setup lang="ts">
import { computed } from 'vue'
import {
  projectCartesianPoints,
  type CartesianAxisRange,
  type CartesianSamples,
} from '../plot/cartesianProjection'

const props = defineProps<{
  traceId: number
  label: string
  unit: string
  samples: CartesianSamples
  range: CartesianAxisRange
}>()

const pathData = computed(() => projectCartesianPoints(props.samples, props.range)
  .map((point, index) => `${index === 0 ? 'M' : 'L'} ${point.x} ${point.y}`)
  .join(' '))
const description = computed(
  () => `Trace ${props.traceId} ${props.label} curve in ${props.unit}, ${props.samples.values.length} samples`,
)
</script>

<template>
  <svg
    v-if="pathData"
    class="trace-curve"
    viewBox="0 0 1 1"
    preserveAspectRatio="none"
    role="img"
    :aria-label="description"
  >
    <path :d="pathData" />
  </svg>
</template>

<style scoped>
/* The rectangular viewport clips crossing segments without changing backend values. */
.trace-curve { position: absolute; inset: 0; z-index: 1; width: 100%; height: 100%; overflow: hidden; color: var(--trace-color, #f2db24); pointer-events: none; }
path { fill: none; stroke: currentColor; stroke-width: 2px; vector-effect: non-scaling-stroke; }
</style>
