<script setup lang="ts">
import { computed } from 'vue'
import type { MeasurementType } from '../api/vnaApi'
import {
  logicalPorts,
  measurementCategories,
  physicalPorts,
  portPairForMeasurement,
  sParameters,
} from './measurementSofttoolModel'

const props = defineProps<{ measurementType: MeasurementType | undefined }>()
// Snapshot identity is the only selected-state authority. Until the command contract exists,
// this component deliberately has no emits and native disabled controls prevent local success.
const portPair = computed(() => portPairForMeasurement(props.measurementType))
</script>

<template>
  <aside class="measurement-softtool" aria-label="Measurement menu">
    <header class="softtool-title">
      <strong>Measurement Menu</strong>
      <span>S-Parameters</span>
    </header>
    <div class="softtool-body">
      <section class="parameter-column" aria-label="S-Parameter selection">
        <div class="selector-row">
          <button type="button" aria-label="Measurement family S" disabled>S</button>
          <button type="button" aria-label="Active logical port pair" disabled>
            {{ portPair }}
          </button>
        </div>
        <div class="parameter-grid">
          <button
            v-for="parameter in sParameters"
            :key="parameter"
            type="button"
            :class="{ active: measurementType === parameter }"
            :aria-pressed="measurementType === parameter"
            disabled
          >
            {{ parameter }}
          </button>
        </div>
        <button class="full-width" type="button" disabled>All S-Params</button>
        <button class="full-width" type="button" disabled>S-Param Wizard <span>›</span></button>
        <h2>Balanced Ports</h2>
        <button class="topology" type="button" aria-label="Balanced Ports topology" disabled>
          <span v-for="port in physicalPorts" :key="port">{{ port }}</span>
          <b aria-hidden="true">••••</b>
          <span v-for="port in logicalPorts" :key="port">{{ port }}</span>
        </button>
      </section>
      <nav class="category-column" aria-label="Measurement categories">
        <button
          v-for="category in measurementCategories"
          :key="category"
          type="button"
          :class="{ active: category === 'S-Params' }"
          disabled
        >
          {{ category }}
        </button>
      </nav>
    </div>
  </aside>
</template>

<style scoped>
.measurement-softtool { min-width: 0; overflow: hidden; background: #10181c; border-left: 2px solid #05090b; }
.softtool-title { display: grid; height: 42px; padding: 5px 8px; background: #25333a; font-size: 11px; }
.softtool-title span { justify-self: end; color: #f0f4f5; }
.softtool-body { display: grid; grid-template-columns: 165px 1fr; height: calc(100% - 42px); }
.parameter-column { min-width: 0; padding: 3px; overflow: hidden; background: #1c282e; }
.selector-row { display: grid; grid-template-columns: 1fr 54px; gap: 3px; }
.selector-row button { height: 35px; }
.parameter-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 3px; margin-top: 3px; }
.parameter-grid button { height: 39px; text-align: left; }
.parameter-grid button.active { border-left: 4px solid #258fd5; background: #647882; }
button { padding: 3px 7px; border: 1px solid #11191d; background: #53656e; color: #f0f3f4; font-size: 11px; }
button:disabled { color: #b5c0c4; background: #3e4b51; opacity: 1; cursor: default; }
.full-width { display: flex; align-items: center; justify-content: space-between; width: 100%; height: 39px; margin-top: 3px; text-align: left; }
h2 { margin: 3px 0 1px; font-size: 10px; font-weight: 600; }
.topology { display: grid; grid-template-columns: 1fr 1fr; width: 100%; height: 51px; color: #dfe6e8; }
.topology b { grid-column: 1 / -1; color: #c8d4d8; letter-spacing: 6px; }
.category-column { display: flex; flex-direction: column; gap: 2px; padding: 3px 2px; background: #11191d; }
.category-column button { min-height: 43px; text-align: left; }
.category-column button.active { border-right: 3px solid #248fd5; background: #384a53; }
</style>
