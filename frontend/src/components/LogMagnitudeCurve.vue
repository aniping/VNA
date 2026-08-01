<script setup lang="ts">
import { computed } from 'vue'
import type { TraceDisplayFrame } from '../api/traceDisplayFrameApi'
import type { CartesianScaleSnapshot } from '../api/vnaApi'
import { projectLogMagnitudePoints } from '../plot/logMagnitudeProjection'

const props = defineProps<{
  frame: TraceDisplayFrame
  scale: CartesianScaleSnapshot
}>()

const pathData = computed(() => projectLogMagnitudePoints(props.frame, props.scale)
  .map((point, index) => `${index === 0 ? 'M' : 'L'} ${point.x} ${point.y}`)
  .join(' '))
const description = computed(
  () => `Trace ${props.frame.traceId} Log Magnitude curve, ${props.frame.values.length} samples`,
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
/* The viewport clips out-of-range dB values without changing their projected coordinates. */
.trace-curve { position: absolute; inset: 0; z-index: 1; width: 100%; height: 100%; overflow: hidden; color: var(--trace-color, #f2db24); pointer-events: none; }
path { fill: none; stroke: currentColor; stroke-width: 2px; vector-effect: non-scaling-stroke; }
</style>
