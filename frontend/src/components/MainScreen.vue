<script setup lang="ts">
import { computed, ref } from 'vue'
import type { StateSnapshot, SweepSettings, TraceSetup as TraceSetupModel } from '../api/vnaApi'
import DiagramGrid from './DiagramGrid.vue'
import HardkeyPanel from './HardkeyPanel.vue'
import MeasurementSofttool from './MeasurementSofttool.vue'
import StimulusSofttool from './StimulusSofttool.vue'
import { isStimulusKey, type HardkeyName, type StimulusKey } from './hardkeyModel'

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
  updateSweep: [channelId: number, sweep: SweepSettings]
}>()

const toolbar = ['↶', '↷', 'Zoom', 'Max', '+ Trace', '+ Marker', 'Delete', 'Print', 'Save', 'Recall']
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const activeSofttool = ref<'measurement' | 'stimulus' | null>('measurement')
const stimulusKey = ref<StimulusKey>('Start')
const activeKey = computed(() => {
  if (activeSofttool.value === 'measurement') return 'Meas'
  if (activeSofttool.value === 'stimulus') return stimulusKey.value
  return null
})
const entityCounts = computed(() => {
  const instrument = props.state?.instrument
  if (!instrument) return 'Ch — · Meas — · Trc — · Win —'
  return `Ch ${instrument.channels.length} · Meas ${instrument.measurements.length}`
    + ` · Trc ${instrument.traces.length} · Win ${instrument.windows.length}`
})

function selectHardkey(key: HardkeyName): void {
  if (key === 'Meas') {
    activeSofttool.value = activeSofttool.value === 'measurement' ? null : 'measurement'
    return
  }
  if (!isStimulusKey(key)) return
  if (!channel.value) return
  stimulusKey.value = key
  activeSofttool.value = 'stimulus'
}

function forwardSweepUpdate(channelId: number, sweep: SweepSettings): void {
  emit('updateSweep', channelId, sweep)
}
</script>

<template>
  <section class="main-screen" aria-label="Main measurement screen">
    <div class="application-workspace" :class="{ 'softtool-visible': activeSofttool }">
      <div class="measurement-stage">
        <header class="toolbar">
          <button v-for="item in toolbar" :key="item" type="button">{{ item }}</button>
          <span class="toolbar-spacer" />
          <span class="instrument-title">Vector Network Analyzer</span>
        </header>
        <DiagramGrid :state="state" />
      </div>

      <MeasurementSofttool
        v-if="activeSofttool === 'measurement'"
        :has-channel="Boolean(channel)"
        :disabled="disabled"
        :busy="busy"
        @create-channel="emit('createChannel', $event)"
        @create-trace="emit('createTrace', $event)"
      />
      <StimulusSofttool
        v-else-if="activeSofttool === 'stimulus' && channel"
        :channel="channel"
        :focus="stimulusKey"
        :disabled="disabled"
        :busy="busy"
        @update-sweep="forwardSweepUpdate"
      />

      <HardkeyPanel
        :active-key="activeKey"
        :has-channel="Boolean(channel)"
        @select="selectHardkey"
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
