<script setup lang="ts">
import { computed, ref } from 'vue'
import type { StateSnapshot, SweepSettings, TraceSetup as TraceSetupModel } from '../api/vnaApi'
import ChannelSetup from './ChannelSetup.vue'
import DiagramGrid from './DiagramGrid.vue'
import HardkeyPanel from './HardkeyPanel.vue'
import TraceSetup from './TraceSetup.vue'

const props = defineProps<{
  state: StateSnapshot | null
  connection: 'connecting' | 'online' | 'offline'
  serviceError: string
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  createChannel: [sweep: SweepSettings]
  createTrace: [setup: TraceSetupModel]
}>()

const toolbar = ['↶', '↷', 'Zoom', 'Max', '+ Trace', '+ Marker', 'Delete', 'Print', 'Save', 'Recall']
const softkeys = ['S-Parameters', 'Wave Quantities', 'Ratios', 'Receivers', 'More…']
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const activeSofttool = ref<'measurement' | null>('measurement')
const softtoolPage = ref<'measurement' | 'sParameters'>('measurement')
const entityCounts = computed(() => {
  const instrument = props.state?.instrument
  if (!instrument) return 'Ch — · Meas — · Trc — · Win —'
  return `Ch ${instrument.channels.length} · Meas ${instrument.measurements.length}`
    + ` · Trc ${instrument.traces.length} · Win ${instrument.windows.length}`
})

</script>

<template>
  <section class="main-screen" aria-label="Main measurement screen">
    <header class="toolbar">
      <button v-for="item in toolbar" :key="item" type="button">{{ item }}</button>
      <span class="toolbar-spacer" />
      <span class="instrument-title">Vector Network Analyzer</span>
    </header>

    <div class="measurement-workspace" :class="{ 'softtool-visible': activeSofttool }">
      <DiagramGrid :state="state" />

      <aside v-if="activeSofttool" class="softtool" aria-label="Softtool panel">
        <div class="softtool-tabs">
          <button class="active" type="button" @click="softtoolPage = 'measurement'">Meas</button>
          <button type="button">Favorites</button>
        </div>
        <h2>{{ softtoolPage === 'measurement' ? 'Measurement' : 'S-Parameters' }}</h2>
        <template v-if="channel && softtoolPage === 'measurement'">
          <button
            v-for="item in softkeys"
            :key="item"
            type="button"
            class="softkey"
            :disabled="item !== 'S-Parameters'"
            @click="softtoolPage = 'sParameters'"
          >
            <span>{{ item }}</span><span aria-hidden="true">›</span>
          </button>
        </template>
        <ChannelSetup
          v-else-if="!channel"
          :disabled="disabled"
          :busy="busy"
          @create-channel="emit('createChannel', $event)"
        />
        <TraceSetup
          v-else
          :disabled="disabled"
          :busy="busy"
          @create-trace="emit('createTrace', $event)"
        />
        <div class="softtool-fill" />
        <button type="button" class="softkey close-key" @click="activeSofttool = null">Close</button>
      </aside>

      <HardkeyPanel
        :active-key="activeSofttool ? 'Meas' : null"
        @select="activeSofttool = 'measurement'; softtoolPage = 'measurement'"
      />
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
