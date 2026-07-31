<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChannelSnapshot, SweepSettings } from '../api/vnaApi'
import type { ChannelKey } from './hardkeyModel'

const props = defineProps<{
  channel: ChannelSnapshot
  mode: ChannelKey
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{ updateSweep: [channelId: number, sweep: SweepSettings] }>()
const powerDbm = ref(0)
const ifBandwidthKHz = ref(1)
const points = ref(201)
const isPowerPage = computed(() => props.mode === 'Power / Bw / Avg')

const candidateSweep = computed<SweepSettings | null>(() => {
  if (isPowerPage.value) {
    if (!Number.isFinite(powerDbm.value)) return null
    if (!Number.isFinite(ifBandwidthKHz.value) || ifBandwidthKHz.value <= 0) return null
    return {
      ...props.channel.sweep,
      powerDbm: powerDbm.value,
      ifBandwidthHz: Math.round(ifBandwidthKHz.value * 1e3),
    }
  }
  if (!Number.isInteger(points.value) || points.value < 2) return null
  return { ...props.channel.sweep, points: points.value }
})

watch(
  () => props.channel.sweep,
  (sweep) => {
    powerDbm.value = sweep.powerDbm
    ifBandwidthKHz.value = sweep.ifBandwidthHz / 1e3
    points.value = sweep.points
  },
  { immediate: true },
)

function apply(): void {
  if (!candidateSweep.value) return
  emit('updateSweep', props.channel.id, candidateSweep.value)
}
</script>

<template>
  <aside class="channel-softtool" aria-label="Channel menu">
    <header>
      <strong>Channel Menu</strong>
      <span>{{ isPowerPage ? 'Power / Bandwidth' : 'Sweep' }}</span>
    </header>
    <h2>Channel {{ channel.id }}</h2>
    <dl>
      <dt>Power</dt><dd>{{ channel.sweep.powerDbm.toFixed(2) }} dBm</dd>
      <dt>IF Bandwidth</dt><dd>{{ (channel.sweep.ifBandwidthHz / 1e3).toFixed(3) }} kHz</dd>
      <dt>Points</dt><dd>{{ channel.sweep.points }}</dd>
    </dl>
    <form @submit.prevent="apply">
      <template v-if="isPowerPage">
        <label for="channel-power">Power</label>
        <div class="value-entry">
          <input id="channel-power" v-model.number="powerDbm" type="number" step="0.1" />
          <span>dBm</span>
        </div>
        <label for="channel-ifbw">IF Bandwidth</label>
        <div class="value-entry">
          <input id="channel-ifbw" v-model.number="ifBandwidthKHz" type="number" min="0.001" step="0.001" />
          <span>kHz</span>
        </div>
      </template>
      <template v-else>
        <label for="channel-points">Sweep Points</label>
        <div class="value-entry">
          <input id="channel-points" v-model.number="points" type="number" min="2" step="1" />
          <span>pts</span>
        </div>
      </template>
      <p v-if="!candidateSweep">Enter a valid value.</p>
      <button type="submit" :disabled="disabled || busy || !candidateSweep">
        {{ busy ? 'Applying…' : 'Apply Settings' }}
      </button>
    </form>
  </aside>
</template>

<style scoped>
.channel-softtool { min-width: 0; overflow: hidden; background: #1c282e; border-left: 2px solid #05090b; }
header { display: grid; height: 42px; padding: 5px 8px; background: #25333a; font-size: 11px; }
header span { justify-self: end; }
h2 { margin: 0; padding: 8px; background: #11191d; font-size: 12px; }
dl { display: grid; grid-template-columns: 104px 1fr; gap: 2px; margin: 3px; font-size: 11px; }
dt, dd { min-height: 43px; margin: 0; padding: 7px; border: 1px solid #11191d; }
dt { background: #53656e; }
dd { display: flex; align-items: center; justify-content: flex-end; background: #28363d; }
form { margin: 12px 5px; padding: 8px; background: #11191d; }
label { display: block; margin: 8px 0 5px; font-size: 11px; }
.value-entry { display: grid; grid-template-columns: 1fr 45px; height: 38px; }
input { min-width: 0; padding: 0 7px; color: #fff; border: 1px solid #71838c; background: #05090b; text-align: right; }
.value-entry span { display: grid; place-items: center; background: #53656e; font-size: 10px; }
p { margin: 6px 0; color: #ffae70; font-size: 10px; }
form button { width: 100%; height: 38px; margin-top: 10px; border: 1px solid #0b4258; background: #268fc5; font-weight: 700; }
form button:disabled { color: #839197; background: #35434a; }
</style>
