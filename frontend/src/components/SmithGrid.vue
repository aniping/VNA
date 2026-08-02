<script setup lang="ts">
import { useId } from 'vue'
import {
  normalizedMagnitudeRadii,
  normalizedReactanceMagnitudes,
  normalizedResistanceCircleValues,
  normalizedResistanceLabelValues,
  formatNormalizedLabel,
  reactanceCircle,
  reactanceLabelPoint,
  resistanceCircle,
  resistanceLabelPoint,
  type SmithGridPoint,
} from './smithGridModel'

const clipId = `smith-unit-${useId()}`
const resistanceCircles = normalizedResistanceCircleValues.map((value) => ({
  value, ...resistanceCircle(value),
}))
const reactanceCircles = normalizedReactanceMagnitudes.flatMap((magnitude) => (
  [magnitude, -magnitude].map((value) => ({ value, ...reactanceCircle(value) }))
))
const resistanceLabels = normalizedResistanceLabelValues.map((value) => ({
  value, ...insetLabel(resistanceLabelPoint(value), 0.96),
}))
const reactanceLabels = normalizedReactanceMagnitudes.flatMap((magnitude) => (
  [magnitude, -magnitude].map((value) => ({
    value, ...insetLabel(reactanceLabelPoint(value), 0.92),
  }))
))

function insetLabel(point: SmithGridPoint, factor: number): SmithGridPoint {
  return { x: point.x * factor, y: point.y * factor }
}

</script>

<template>
  <svg
    class="smith-grid"
    viewBox="-1 -1 2 2"
    preserveAspectRatio="xMidYMid meet"
    aria-hidden="true"
  >
    <defs>
      <clipPath :id="clipId">
        <circle cx="0" cy="0" r="1" />
      </clipPath>
    </defs>
    <g :clip-path="`url(#${clipId})`">
      <circle
        v-for="radius in normalizedMagnitudeRadii"
        :key="`m-${radius}`"
        cx="0"
        cy="0"
        :r="radius"
        class="grid-line magnitude-circle"
      />
      <circle
        v-for="circle in resistanceCircles"
        :key="`r-${circle.value}`"
        :cx="circle.centerX"
        :cy="circle.centerY"
        :r="circle.radius"
        class="grid-line resistance-circle"
      />
      <circle
        v-for="circle in reactanceCircles"
        :key="`x-${circle.value}`"
        :cx="circle.centerX"
        :cy="circle.centerY"
        :r="circle.radius"
        class="grid-line reactance-circle"
      />
    </g>
    <circle cx="0" cy="0" r="1" class="grid-line grid-major outer-circle" />
    <line x1="-1" y1="0" x2="1" y2="0" class="grid-line grid-major center-axis" />
    <text
      v-for="label in resistanceLabels"
      :key="`rl-${label.value}`"
      :x="label.x"
      :y="label.y - 0.025"
      class="smith-label resistance-label"
    >{{ formatNormalizedLabel(label.value) }}</text>
    <text
      v-for="label in reactanceLabels"
      :key="`xl-${label.value}`"
      :x="label.x"
      :y="label.y"
      class="smith-label reactance-label"
    >{{ formatNormalizedLabel(label.value) }}</text>
  </svg>
</template>

<style scoped>
.smith-grid { position: absolute; inset: 0; width: 100%; height: 100%; overflow: hidden; pointer-events: none; }
.grid-line { fill: none; stroke: #4a606a; stroke-width: 1px; vector-effect: non-scaling-stroke; }
.grid-major { stroke: #607580; }
.magnitude-circle { opacity: .58; }
.smith-label { fill: #8ca0a8; stroke: none; font-size: .04px; text-anchor: middle; dominant-baseline: middle; }
</style>
