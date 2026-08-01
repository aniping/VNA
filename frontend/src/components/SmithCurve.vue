<script setup lang="ts">
import { computed } from 'vue'
import {
  projectSmithPoints,
  type SmithComplexPoint,
} from '../plot/smithProjection'

const props = defineProps<{
  traceId: number
  samples: readonly SmithComplexPoint[]
}>()

const pathData = computed(() => projectSmithPoints(props.samples)
  .map((point, index) => `${index === 0 ? 'M' : 'L'} ${point.x} ${point.y}`)
  .join(' '))
const description = computed(
  () => `Trace ${props.traceId} Smith curve, ${props.samples.length} samples`,
)
</script>

<template>
  <svg
    v-if="pathData"
    class="smith-curve"
    viewBox="-1 -1 2 2"
    preserveAspectRatio="xMidYMid meet"
    role="img"
    :aria-label="description"
  >
    <path :d="pathData" />
  </svg>
</template>

<style scoped>
/* "meet" makes the unit radius half the short side; rectangular clipping preserves corners
   outside the unit circle instead of inventing a passive-only circular clip. */
.smith-curve { position: absolute; inset: 0; z-index: 1; width: 100%; height: 100%; overflow: hidden; color: var(--trace-color, #f2db24); pointer-events: none; }
path { fill: none; stroke: currentColor; stroke-width: 2px; vector-effect: non-scaling-stroke; }
</style>
