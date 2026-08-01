<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { LiveDisplayConnection } from '../api/liveDisplaySession'
import type {
  StateSnapshot,
  SweepSettings,
  TraceFormat,
} from '../api/vnaApi'
import type { TraceDisplayFrame } from '../api/traceDisplayFrameApi'
import ChannelSofttool from './ChannelSofttool.vue'
import DiagramGrid from './DiagramGrid.vue'
import FormatSofttool from './FormatSofttool.vue'
import HardkeyPanel from './HardkeyPanel.vue'
import InstrumentToolbar from './InstrumentToolbar.vue'
import MeasurementSofttool from './MeasurementSofttool.vue'
import ScaleSofttool from './ScaleSofttool.vue'
import StimulusSofttool from './StimulusSofttool.vue'
import { selectWorkspacePresentation } from './workspacePresentation'
import {
  isChannelKey,
  isStimulusKey,
  type ChannelKey,
  type HardkeyName,
  type StimulusKey,
} from './hardkeyModel'

const props = defineProps<{
  state: StateSnapshot | null
  connection: LiveDisplayConnection
  serviceError: string
  displayError: string
  busy: boolean
  frames: ReadonlyMap<number, TraceDisplayFrame>
}>()
const emit = defineEmits<{
  updateSweep: [channelId: number, sweep: SweepSettings]
  updateTraceFormat: [traceId: number, format: TraceFormat]
  updateTraceScalePerDivision: [traceId: number, value: number]
}>()

const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const activeTraceId = ref<number>()
const activeTrace = computed(() => {
  const traces = props.state?.instrument.traces ?? []
  return traces.find((trace) => trace.id === activeTraceId.value) ?? traces[0]
})
const activeMeasurement = computed(() => props.state?.instrument.measurements
  .find((measurement) => measurement.id === activeTrace.value?.measurementId))
const isDiagramMaximized = ref(false)
const canMaximizeDiagram = computed(() => Boolean(activeTrace.value))
const workspace = computed(() => selectWorkspacePresentation({
  state: props.state,
  connection: props.connection,
  hasFrame: props.frames.size > 0,
  displayError: props.displayError,
}))
const acquisitionStatus = computed(() => {
  if (!channel.value) return 'Sweep — · Trigger —'
  const sweepMode = channel.value.sweepMode.replace(/^./, (letter) => letter.toUpperCase())
  const trigger = channel.value.triggerSource.replace(/^./, (letter) => letter.toUpperCase())
  return `${sweepMode} · ${trigger}`
})
// Nullable scale is the display-model capability seam; format must not re-derive support.
const activeScale = computed(() => activeTrace.value?.scale ?? null)
const activeSofttool = ref<'measurement' | 'format' | 'scale' | 'stimulus' | 'channel' | null>(
  null,
)
const stimulusKey = ref<StimulusKey>('Start')
const channelKey = ref<ChannelKey>('Power / Bw / Avg')
const activeKey = computed(() => {
  if (activeSofttool.value === 'measurement') return 'Meas'
  if (activeSofttool.value === 'format') return 'Format'
  if (activeSofttool.value === 'scale') return 'Scale'
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

watch(activeScale, (scale) => {
  // Authority can remove Scale support after a Trace change, so no stale tool may remain open.
  if (!scale && activeSofttool.value === 'scale') activeSofttool.value = null
})

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
  if (key === 'Scale' && activeScale.value) {
    activeSofttool.value = activeSofttool.value === 'scale' ? null : 'scale'
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

function forwardScalePerDivisionUpdate(traceId: number, value: number): void {
  emit('updateTraceScalePerDivision', traceId, value)
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
        <div
          v-if="!workspace.showDiagrams"
          class="workspace-notice"
          :class="workspace.mode"
          role="status"
          aria-live="polite"
        >
          <strong>{{ workspace.headline }}</strong>
          <span>{{ workspace.detail }}</span>
        </div>
        <DiagramGrid
          v-else
          :state="state"
          :frames="frames"
          :active-trace-id="activeTrace?.id"
          :maximized="isDiagramMaximized"
          @select-trace="activeTraceId = $event"
        />
        <div v-if="workspace.mode === 'stale'" class="workspace-stale" role="status">
          {{ workspace.statusLabel }}
        </div>
      </div>

      <MeasurementSofttool
        v-if="activeSofttool === 'measurement'"
        :measurement-type="activeMeasurement?.type"
      />
      <FormatSofttool
        v-else-if="activeSofttool === 'format' && activeTrace"
        :trace="activeTrace"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @update-format="forwardTraceFormatUpdate"
      />
      <ScaleSofttool
        v-else-if="activeSofttool === 'scale' && activeTrace && activeScale"
        :trace-id="activeTrace.id"
        :scale="activeScale"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @update-scale-per-division="forwardScalePerDivisionUpdate"
      />
      <StimulusSofttool
        v-else-if="activeSofttool === 'stimulus' && channel"
        :channel="channel"
        :focus="stimulusKey"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @update-sweep="forwardSweepUpdate"
      />
      <ChannelSofttool
        v-else-if="activeSofttool === 'channel' && channel"
        :channel="channel"
        :mode="channelKey"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @update-sweep="forwardSweepUpdate"
      />

      <HardkeyPanel
        :active-key="activeKey"
        :has-channel="Boolean(channel)"
        :has-trace="Boolean(activeTrace)"
        :has-scale="Boolean(activeScale)"
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
      <span
        class="status-pill"
        :class="workspace.statusTone"
        :title="displayError || serviceError"
      >
        {{ workspace.statusLabel }}
      </span>
      <span title="Sweep mode · Trigger source">{{ acquisitionStatus }}</span>
      <span>Revision {{ state?.stateRevision ?? '—' }}</span>
      <span class="entity-counts">{{ entityCounts }}</span>
      <time>{{ serviceError || 'Local session' }}</time>
    </nav>
  </section>
</template>
