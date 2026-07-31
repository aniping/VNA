<script setup lang="ts">
import type { SweepSettings, TraceSetup as TraceSetupModel } from '../api/vnaApi'
import ChannelSetup from './ChannelSetup.vue'
import TraceSetup from './TraceSetup.vue'

defineProps<{ hasChannel: boolean; disabled: boolean; busy: boolean }>()
const emit = defineEmits<{
  createChannel: [sweep: SweepSettings]
  createTrace: [setup: TraceSetupModel]
}>()

const categories = [
  'S-Params',
  'Wave',
  'Ratio / Harmonics',
  'Noise Figure',
  'Intermodulation',
  'Gain Compression',
  'Power Sensor',
  'Spectrum',
  'DC Meas',
]
</script>

<template>
  <aside class="measurement-softtool" aria-label="Measurement menu">
    <header class="softtool-title">
      <strong>Measurement Menu</strong>
      <span>S-Parameters</span>
    </header>
    <div class="softtool-tabs">
      <button type="button">⚙&nbsp; Measurement Setup <span>›</span></button>
      <button class="active" type="button">S-Params</button>
    </div>
    <div class="softtool-body">
      <section class="setup-column">
        <ChannelSetup
          v-if="!hasChannel"
          :disabled="disabled"
          :busy="busy"
          @create-channel="emit('createChannel', $event)"
        />
        <template v-else>
          <h2>Measurement</h2>
          <button class="measurement-selector" type="button">
            <span>S-Parameters</span><span>▼</span>
          </button>
          <TraceSetup
            :disabled="disabled"
            :busy="busy"
            @create-trace="emit('createTrace', $event)"
          />
        </template>
        <div class="topology">
          <h2>Topology</h2>
          <div><span>Port 1</span><b>····</b><span>Port 2</span></div>
        </div>
      </section>
      <nav class="category-column" aria-label="Measurement categories">
        <button
          v-for="category in categories"
          :key="category"
          type="button"
          :class="{ active: category === 'S-Params' }"
          :disabled="category !== 'S-Params'"
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
.softtool-tabs { display: grid; grid-template-columns: 165px 1fr; height: 43px; border-bottom: 2px solid #0a0f12; }
.softtool-tabs button { display: flex; align-items: center; justify-content: space-between; padding: 4px 8px; border: 1px solid #11191d; background: #51636c; font-size: 11px; text-align: left; }
.softtool-tabs button.active { border-right: 3px solid #258ed3; background: #374951; }
.softtool-body { display: grid; grid-template-columns: 165px 1fr; height: calc(100% - 85px); }
.setup-column { min-width: 0; padding: 3px; overflow: hidden; background: #1c282e; }
.setup-column h2 { margin: 2px 0; padding: 4px 3px; font-size: 11px; font-weight: 600; }
.measurement-selector { display: flex; align-items: center; justify-content: space-between; width: 100%; height: 35px; padding: 0 7px; border: 1px solid #172126; background: #53656e; font-size: 11px; }
.category-column { display: flex; flex-direction: column; gap: 2px; padding: 3px 2px; background: #11191d; }
.category-column button { flex: 1; min-height: 43px; padding: 3px 5px; border: 1px solid #0b1114; background: #172126; font-size: 11px; line-height: 1.05; text-align: left; }
.category-column button.active { border-right: 3px solid #248fd5; background: #384a53; }
.category-column button:disabled { color: #f0f3f4; opacity: 1; }
.topology { margin-top: 8px; }
.topology div { display: flex; align-items: center; justify-content: space-between; padding: 8px 6px; color: #dfe6e8; background: #5b6d75; font-size: 10px; }
.topology b { color: #c8d4d8; letter-spacing: 1px; }
</style>
