<script setup lang="ts">
import { computed } from 'vue'
import {
  projectCartesianSegments,
  type CartesianAxisRange,
  type CartesianSegmentedSamples,
} from '../plot/cartesianProjection'
import { segmentedSvgPath } from '../plot/segmentedSvgPath'

const props = defineProps<{
  traceId: number
  label: string
  unit: string
  samples: CartesianSegmentedSamples
  range: CartesianAxisRange
}>()

const pathData = computed(() => segmentedSvgPath(
  projectCartesianSegments(props.samples, props.range),
))
const sampleCount = computed(() => props.samples.segments
  .reduce((total, segment) => total + segment.values.length, 0))
const description = computed(
  () => `Trace ${props.traceId} ${props.label} curve in ${props.unit}, ${sampleCount.value} samples`,
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
