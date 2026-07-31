<script setup lang="ts">
import { reactive } from 'vue'
import type { SweepSettings } from '../api/vnaApi'

defineProps<{ disabled: boolean; busy: boolean }>()
const emit = defineEmits<{ createChannel: [sweep: SweepSettings] }>()

const form = reactive({
  startMHz: 10,
  stopGHz: 26.5,
  points: 201,
  ifBandwidthKHz: 10,
  powerDbm: -10,
})

function submit(): void {
  emit('createChannel', {
    startFrequencyHz: Math.round(form.startMHz * 1e6),
    stopFrequencyHz: Math.round(form.stopGHz * 1e9),
    points: Math.round(form.points),
    ifBandwidthHz: Math.round(form.ifBandwidthKHz * 1e3),
    powerDbm: form.powerDbm,
  })
}
</script>

<template>
  <form class="channel-setup" @submit.prevent="submit">
    <h3>New Channel</h3>
    <label>Start <span><input v-model.number="form.startMHz" type="number" min="0" /> MHz</span></label>
    <label>Stop <span><input v-model.number="form.stopGHz" type="number" min="0" step="0.1" /> GHz</span></label>
    <label>Points <span><input v-model.number="form.points" type="number" min="2" /> pts</span></label>
    <label>IFBW <span><input v-model.number="form.ifBandwidthKHz" type="number" min="0" /> kHz</span></label>
    <label>Power <span><input v-model.number="form.powerDbm" type="number" step="0.1" /> dBm</span></label>
    <button type="submit" :disabled="disabled || busy">
      {{ busy ? 'Applying…' : 'Create Channel' }}
    </button>
  </form>
</template>
