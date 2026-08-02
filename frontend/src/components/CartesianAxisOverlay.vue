<script setup lang="ts">
import { computed } from 'vue'
import {
  cartesianAxisTicks,
  formatCartesianAxisTick,
  type CartesianAxisModel,
} from './cartesianAxisModel'

const props = defineProps<{ axis: CartesianAxisModel }>()
const ticks = computed(() => cartesianAxisTicks(props.axis))
// ZNB counts Ref Pos upward from the bottom of its ten-division Cartesian grid.
const referenceTop = computed(() => `${(10 - props.axis.referencePosition) * 10}%`)

function tickTop(index: number): string {
  return `${index * 10}%`
}

function tickClass(index: number): string {
  if (index === 0) return 'edge-top'
  if (index === ticks.value.length - 1) return 'edge-bottom'
  return ''
}

const formatTick = formatCartesianAxisTick
</script>

<template>
  <div class="cartesian-axis" aria-hidden="true">
    <span
      v-for="(tick, index) in ticks"
      :key="index"
      class="axis-tick"
      :class="tickClass(index)"
      :style="{ top: tickTop(index) }"
    >
      {{ formatTick(tick, axis.unit) }}
    </span>
    <span class="reference-line" :style="{ top: referenceTop }">
      <span class="reference-marker" />
    </span>
  </div>
</template>

<style scoped>
.cartesian-axis { position: absolute; inset: 0; z-index: 2; overflow: hidden; pointer-events: none; }
.axis-tick { position: absolute; left: 4px; color: #8ca0a8; font-size: 10px; line-height: 1; transform: translateY(-50%); }
.axis-tick.edge-top { top: 3px !important; transform: none; }
.axis-tick.edge-bottom { top: auto !important; bottom: 3px; transform: none; }
.reference-line { position: absolute; right: 0; left: 0; border-top: 1px dashed var(--trace-color); }
.reference-marker { position: absolute; top: -5px; right: 0; width: 0; height: 0; border-top: 5px solid transparent; border-right: 8px solid var(--trace-color); border-bottom: 5px solid transparent; }
</style>
