<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChannelSnapshot, SweepSettings } from '../api/vnaApi'
import type { StimulusKey } from './hardkeyModel'

const props = defineProps<{
  channel: ChannelSnapshot
  focus: StimulusKey
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{ updateSweep: [channelId: number, sweep: SweepSettings] }>()
const valueGHz = ref(0)

const startGHz = computed(() => props.channel.sweep.startFrequencyHz / 1e9)
const stopGHz = computed(() => props.channel.sweep.stopFrequencyHz / 1e9)
const centerGHz = computed(() => (startGHz.value + stopGHz.value) / 2)
const spanGHz = computed(() => stopGHz.value - startGHz.value)
const entries = computed(() => [
  { key: 'Start', value: startGHz.value },
  { key: 'Stop', value: stopGHz.value },
  { key: 'Center', value: centerGHz.value },
  { key: 'Span', value: spanGHz.value },
] as const)

const candidateSweep = computed<SweepSettings | null>(() => {
  if (!Number.isFinite(valueGHz.value) || valueGHz.value < 0) return null
  const valueHz = Math.round(valueGHz.value * 1e9)
  let start = props.channel.sweep.startFrequencyHz
  let stop = props.channel.sweep.stopFrequencyHz
  const currentSpan = stop - start
  if (props.focus === 'Start') start = valueHz
  if (props.focus === 'Stop') stop = valueHz
  if (props.focus === 'Center') {
    start = Math.round(valueHz - currentSpan / 2)
    stop = Math.round(valueHz + currentSpan / 2)
  }
  if (props.focus === 'Span') {
    const center = (start + stop) / 2
    start = Math.round(center - valueHz / 2)
    stop = Math.round(center + valueHz / 2)
  }
  if (start < 0 || start >= stop) return null
  return { ...props.channel.sweep, startFrequencyHz: start, stopFrequencyHz: stop }
})

watch(
  () => [props.focus, props.channel.sweep.startFrequencyHz, props.channel.sweep.stopFrequencyHz],
  () => {
    valueGHz.value = entries.value.find((entry) => entry.key === props.focus)?.value ?? 0
  },
  { immediate: true },
)

function apply(): void {
  if (!candidateSweep.value) return
  emit('updateSweep', props.channel.id, candidateSweep.value)
}
</script>

<template>
  <aside class="stimulus-softtool" aria-label="Stimulus menu">
    <header>
      <strong>Stimulus Menu</strong>
      <span>{{ focus }} Frequency</span>
    </header>
    <h2>Frequency</h2>
    <dl>
      <template v-for="entry in entries" :key="entry.key">
        <dt :class="{ active: entry.key === focus }">{{ entry.key }}</dt>
        <dd>{{ entry.value.toFixed(6) }} GHz</dd>
      </template>
    </dl>
    <form @submit.prevent="apply">
      <label :for="`stimulus-${focus}`">{{ focus }}</label>
      <div class="value-entry">
        <input
          :id="`stimulus-${focus}`"
          v-model.number="valueGHz"
          type="number"
          min="0"
          step="0.001"
          autofocus
        />
        <span>GHz</span>
      </div>
      <p v-if="!candidateSweep">Start must remain below Stop.</p>
      <button type="submit" :disabled="disabled || busy || !candidateSweep">
        {{ busy ? 'Applying…' : `Apply ${focus}` }}
      </button>
    </form>
  </aside>
</template>

<style scoped>
.stimulus-softtool { min-width: 0; overflow: hidden; background: #1c282e; border-left: 2px solid #05090b; }
header { display: grid; height: 42px; padding: 5px 8px; background: #25333a; font-size: 11px; }
header span { justify-self: end; }
h2 { margin: 0; padding: 8px; background: #11191d; font-size: 12px; }
dl { display: grid; grid-template-columns: 92px 1fr; gap: 2px; margin: 3px; font-size: 11px; }
dt, dd { min-height: 42px; margin: 0; padding: 7px; border: 1px solid #11191d; background: #52646d; }
dt.active { border-left: 4px solid #349bea; background: #65808d; }
dd { display: flex; align-items: center; justify-content: flex-end; background: #28363d; }
form { margin: 12px 5px; padding: 8px; background: #11191d; }
label { display: block; margin-bottom: 6px; font-size: 12px; }
.value-entry { display: grid; grid-template-columns: 1fr 43px; height: 38px; }
input { min-width: 0; padding: 0 7px; color: #fff; border: 1px solid #71838c; background: #05090b; text-align: right; }
.value-entry span { display: grid; place-items: center; background: #53656e; font-size: 11px; }
p { margin: 6px 0; color: #ffae70; font-size: 10px; }
form button { width: 100%; height: 38px; margin-top: 8px; border: 1px solid #0b4258; background: #268fc5; font-weight: 700; }
form button:disabled { color: #839197; background: #35434a; }
</style>
