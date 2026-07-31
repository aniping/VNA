<script setup lang="ts">
import { reactive } from 'vue'
import type { TraceSetup } from '../api/vnaApi'

defineProps<{ disabled: boolean; busy: boolean }>()
const emit = defineEmits<{ createTrace: [setup: TraceSetup] }>()
const form = reactive<TraceSetup>({
  measurementType: 'S11',
  format: 'logMagnitude',
})
</script>

<template>
  <form class="trace-setup" @submit.prevent="emit('createTrace', { ...form })">
    <div class="parameter-grid" aria-label="S parameter">
      <button type="button" :class="{ active: form.measurementType === 'S11' }" @click="form.measurementType = 'S11'">S11</button>
      <button type="button" disabled>S12</button>
      <button type="button" :class="{ active: form.measurementType === 'S21' }" @click="form.measurementType = 'S21'">S21</button>
      <button type="button" disabled>S22</button>
    </div>
    <label class="format-row">
      <span>Format</span>
      <select v-model="form.format">
        <option value="logMagnitude">Log Magnitude</option>
        <option value="phase">Phase</option>
        <option value="smith">Smith</option>
      </select>
    </label>
    <button type="submit" :disabled="disabled || busy">
      {{ busy ? 'Applying…' : 'Create Trace' }}
    </button>
  </form>
</template>

<style scoped>
.trace-setup { margin-top: 3px; }
.parameter-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 3px; }
.parameter-grid button { height: 39px; border: 1px solid #11191d; background: #53656e; font-size: 11px; text-align: left; }
.parameter-grid button.active { border-left: 4px solid #258fd5; background: #647882; }
.parameter-grid button:disabled { color: #dce3e5; opacity: .65; }
.format-row { display: grid; gap: 3px; margin-top: 5px; font-size: 10px; }
.format-row select { width: 100%; height: 31px; padding: 0 4px; color: #fff; border: 1px solid #72838b; background: #11191d; font-size: 11px; }
.trace-setup > button { width: 100%; height: 35px; margin-top: 5px; border: 1px solid #0a3b4d; background: #278ec4; font-size: 11px; font-weight: 700; }
.trace-setup > button:disabled { color: #829096; background: #35434a; }
</style>
