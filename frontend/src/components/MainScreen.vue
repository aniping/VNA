<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type {
  StateSnapshot,
  SweepSettings,
  TraceFormat,
  TraceSetup as TraceSetupModel,
} from '../api/vnaApi'
import ChannelSofttool from './ChannelSofttool.vue'
import DiagramGrid from './DiagramGrid.vue'
import FormatSofttool from './FormatSofttool.vue'
import HardkeyPanel from './HardkeyPanel.vue'
import InstrumentToolbar from './InstrumentToolbar.vue'
import MeasurementSofttool from './MeasurementSofttool.vue'
import StimulusSofttool from './StimulusSofttool.vue'
import {
  isChannelKey,
  isStimulusKey,
  type ChannelKey,
  type HardkeyName,
  type StimulusKey,
} from './hardkeyModel'

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
  updateTraceFormat: [traceId: number, format: TraceFormat]
}>()

const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const activeTraceId = ref<number>()
const activeTrace = computed(() => {
  const traces = props.state?.instrument.traces ?? []
  return traces.find((trace) => trace.id === activeTraceId.value) ?? traces[0]
})
const isDiagramMaximized = ref(false)
const canMaximizeDiagram = computed(() => Boolean(activeTrace.value))
const activeSofttool = ref<'measurement' | 'format' | 'stimulus' | 'channel' | null>('measurement')
const stimulusKey = ref<StimulusKey>('Start')
const channelKey = ref<ChannelKey>('Power / Bw / Avg')
const activeKey = computed(() => {
  if (activeSofttool.value === 'measurement') return 'Meas'
  if (activeSofttool.value === 'format') return 'Format'
  if (activeSofttool.value === 'stimulus') return stimulusKey.value
  if (activeSofttool.value === 'channel') return channelKey.value
  return null
})
const entityCounts = computed(() => {
  const instrument = props.state?.instrument
  if (!instrument) return 'Ch — · Meas — · Trc — · Win —'
  return `Ch ${instrument.channels.length} · Meas ${instrument.measurements.length}`
    + ` · Trc ${instrument.traces.length} · Win ${instrument.windows.length}`
})

watch(activeTrace, (trace) => {
  activeTraceId.value = trace?.id
  if (!trace) isDiagramMaximized.value = false
}, { immediate: true })

function toggleDiagramMaximized(): void {
  if (!canMaximizeDiagram.value) return
  isDiagramMaximized.value = !isDiagramMaximized.value
}

function selectHardkey(key: HardkeyName): void {
  if (key === 'Meas') {
    activeSofttool.value = activeSofttool.value === 'measurement' ? null : 'measurement'
    return
  }
  if (key === 'Format' && activeTrace.value) {
    activeSofttool.value = activeSofttool.value === 'format' ? null : 'format'
    return
  }
  if (isStimulusKey(key) && channel.value) {
    stimulusKey.value = key
    activeSofttool.value = 'stimulus'
  }
  if (isChannelKey(key) && channel.value) {
    channelKey.value = key
    activeSofttool.value = 'channel'
  }
}

function forwardSweepUpdate(channelId: number, sweep: SweepSettings): void {
  emit('updateSweep', channelId, sweep)
}

function forwardTraceFormatUpdate(traceId: number, format: TraceFormat): void {
  emit('updateTraceFormat', traceId, format)
}
</script>

<template>
  <section class="main-screen" aria-label="Main measurement screen">
    <div class="application-workspace" :class="{ 'softtool-visible': activeSofttool }">
      <div class="measurement-stage">
        <InstrumentToolbar
          :maximized="isDiagramMaximized"
          :can-maximize="canMaximizeDiagram"
          @toggle-maximize="toggleDiagramMaximized"
        />
        <DiagramGrid
          :state="state"
          :active-trace-id="activeTrace?.id"
          :maximized="isDiagramMaximized"
          @select-trace="activeTraceId = $event"
        />
      </div>

      <MeasurementSofttool
        v-if="activeSofttool === 'measurement'"
        :has-channel="Boolean(channel)"
        :disabled="disabled"
        :busy="busy"
        @create-channel="emit('createChannel', $event)"
        @create-trace="emit('createTrace', $event)"
      />
      <FormatSofttool
        v-else-if="activeSofttool === 'format' && activeTrace"
        :trace="activeTrace"
        :disabled="disabled"
        :busy="busy"
        @update-format="forwardTraceFormatUpdate"
      />
      <StimulusSofttool
        v-else-if="activeSofttool === 'stimulus' && channel"
        :channel="channel"
        :focus="stimulusKey"
        :disabled="disabled"
        :busy="busy"
        @update-sweep="forwardSweepUpdate"
      />
      <ChannelSofttool
        v-else-if="activeSofttool === 'channel' && channel"
        :channel="channel"
        :mode="channelKey"
        :disabled="disabled"
        :busy="busy"
        @update-sweep="forwardSweepUpdate"
      />

      <HardkeyPanel
        :active-key="activeKey"
        :has-channel="Boolean(channel)"
        :has-trace="Boolean(activeTrace)"
        @select="selectHardkey"
      />
    </div>

    <nav class="menu-bar" aria-label="Application menu">
      <button
        v-for="item in menus"
        :key="item"
        type="button"
        :aria-label="`${item}, not supported`"
        :title="`${item}, not supported`"
        disabled
      >
        {{ item }}
      </button>
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
