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
    <h3>S-Parameter Trace</h3>
    <label>
      Measurement
      <select v-model="form.measurementType">
        <option value="S11">S11</option>
        <option value="S21">S21</option>
      </select>
    </label>
    <label>
      Format
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
