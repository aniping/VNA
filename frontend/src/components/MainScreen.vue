<script setup lang="ts">
import { computed } from 'vue'
import type { StateSnapshot } from '../api/vnaApi'
import type { SweepSettings } from '../api/vnaApi'
import ChannelSetup from './ChannelSetup.vue'

const props = defineProps<{
  state: StateSnapshot | null
  connection: 'connecting' | 'online' | 'offline'
  serviceError: string
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{ createChannel: [sweep: SweepSettings] }>()

const toolbar = ['↶', '↷', 'Zoom', 'Max', '+ Trace', '+ Marker', 'Delete', 'Print', 'Save', 'Recall']
const softkeys = ['S-Parameters', 'Wave Quantities', 'Ratios', 'Receivers', 'More…']
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const trace = computed(() => props.state?.instrument.traces[0])
const windowState = computed(() => props.state?.instrument.windows[0])
const measurement = computed(() => props.state?.instrument.measurements.find(
  (item) => item.id === trace.value?.measurementId,
))

const traceName = computed(() => {
  if (!trace.value) return 'No active trace'
  return `${measurement.value?.type ?? 'Unknown'} · ${trace.value.format}`
})

const entityCounts = computed(() => {
  const instrument = props.state?.instrument
  if (!instrument) return 'Ch — · Meas — · Trc — · Win —'
  return `Ch ${instrument.channels.length} · Meas ${instrument.measurements.length}`
    + ` · Trc ${instrument.traces.length} · Win ${instrument.windows.length}`
})

function frequency(value: number | undefined): string {
  if (value === undefined) return '—'
  if (value >= 1e9) return `${(value / 1e9).toFixed(3)} GHz`
  if (value >= 1e6) return `${(value / 1e6).toFixed(3)} MHz`
  return `${value} Hz`
}
</script>

<template>
  <section class="main-screen" aria-label="Main measurement screen">
    <header class="toolbar">
      <button v-for="item in toolbar" :key="item" type="button">{{ item }}</button>
      <span class="toolbar-spacer" />
      <span class="instrument-title">Vector Network Analyzer</span>
    </header>

    <div class="measurement-workspace">
      <section class="diagram" aria-label="Cartesian measurement diagram">
        <header class="trace-strip">
          <span class="trace-index">{{ trace ? `Trc${trace.id}` : 'Trc —' }}</span>
          <span class="trace-name">{{ traceName }}</span>
        </header>
        <div class="plot-area">
          <span class="plot-reference">{{ windowState?.id ?? '—' }}</span>
          <span class="plot-empty">No measurement data</span>
        </div>
        <footer class="channel-row">
          <span>{{ channel ? `Ch${channel.id}` : 'No Ch' }}</span>
          <span>Start {{ frequency(channel?.sweep.startFrequencyHz) }}</span>
          <span>Stop {{ frequency(channel?.sweep.stopFrequencyHz) }}</span>
          <span>Points {{ channel?.sweep.points ?? '—' }}</span>
          <span>IFBW {{ frequency(channel?.sweep.ifBandwidthHz) }}</span>
          <span>Power {{ channel ? `${channel.sweep.powerDbm} dBm` : '—' }}</span>
        </footer>
      </section>

      <aside class="softtool" aria-label="Softtool panel">
        <div class="softtool-tabs">
          <button class="active" type="button">Meas</button>
          <button type="button">Favorites</button>
        </div>
        <h2>Measurement</h2>
        <template v-if="channel">
          <button v-for="item in softkeys" :key="item" type="button" class="softkey">
            <span>{{ item }}</span><span aria-hidden="true">›</span>
          </button>
        </template>
        <ChannelSetup
          v-else
          :disabled="disabled"
          :busy="busy"
          @create-channel="emit('createChannel', $event)"
        />
        <div class="softtool-fill" />
        <button type="button" class="softkey close-key">Close</button>
      </aside>
    </div>

    <nav class="menu-bar" aria-label="Application menu">
      <button v-for="item in menus" :key="item" type="button">{{ item }}</button>
      <span class="menu-spacer" />
      <span class="status-pill" :class="connection" :title="serviceError">
        {{ connection.toUpperCase() }}
      </span>
      <span>Revision {{ state?.stateRevision ?? '—' }}</span>
      <span class="entity-counts">{{ entityCounts }}</span>
      <time>{{ serviceError || 'Local session' }}</time>
    </nav>
  </section>
</template>
