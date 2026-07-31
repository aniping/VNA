<script setup lang="ts">
import { computed } from 'vue'
import type { StateSnapshot } from '../api/vnaApi'

const props = defineProps<{
  state: StateSnapshot | null
  connection: 'connecting' | 'online' | 'offline'
  serviceError: string
}>()

const toolbar = ['↶', '↷', 'Zoom', 'Max', '+ Trace', '+ Marker', 'Delete', 'Print', 'Save', 'Recall']
const softkeys = ['S-Parameters', 'Wave Quantities', 'Ratios', 'Receivers', 'More…']
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])

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
          <span class="trace-index">Trc1</span>
          <span class="trace-name">No active trace</span>
        </header>
        <div class="plot-area">
          <span class="plot-reference">1</span>
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
        <button v-for="item in softkeys" :key="item" type="button" class="softkey">
          <span>{{ item }}</span><span aria-hidden="true">›</span>
        </button>
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
      <time>{{ serviceError || 'Local session' }}</time>
    </nav>
  </section>
</template>
