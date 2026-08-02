<script setup lang="ts">
import { computed } from 'vue'
import {
  projectSmithSegments,
  type SmithComplexPoint,
} from '../plot/smithProjection'
import { segmentedSvgPath } from '../plot/segmentedSvgPath'

const props = defineProps<{
  traceId: number
  segments: readonly (readonly SmithComplexPoint[])[]
}>()

const pathData = computed(() => segmentedSvgPath(projectSmithSegments(props.segments)))
const sampleCount = computed(() => props.segments
  .reduce((total, segment) => total + segment.length, 0))
const description = computed(
  () => `Trace ${props.traceId} Smith curve, ${sampleCount.value} samples`,
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
