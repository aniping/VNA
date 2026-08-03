<script setup lang="ts">
import { computed, ref, toRef, watch } from 'vue'
import type { LiveDisplayConnection } from '../api/liveDisplaySession'
import type { LiveDisplayState } from '../api/displayFrameSetState'
import type {
  MeasurementType,
  StateSnapshot,
  SweepMode,
  SweepSettings,
  TraceFormat,
} from '../api/vnaApi'
import ChannelSofttool from './ChannelSofttool.vue'
import DiagramGrid from './DiagramGrid.vue'
import FormatSofttool from './FormatSofttool.vue'
import HardkeyPanel from './HardkeyPanel.vue'
import InstrumentToolbar from './InstrumentToolbar.vue'
import InstrumentStatusBar from './InstrumentStatusBar.vue'
import MeasurementSofttool from './MeasurementSofttool.vue'
import ScaleSofttool from './ScaleSofttool.vue'
import StimulusSofttool from './StimulusSofttool.vue'
import SweepSofttool from './SweepSofttool.vue'
import { nextSweepPage, type SweepSofttoolPage } from './sweepSofttoolModel'
import { selectWorkspacePresentation } from './workspacePresentation'
import { useActiveTrace } from './useActiveTrace'
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
  display: LiveDisplayState
}>()
const emit = defineEmits<{
  ensureAllSParameters: [traceId: number]
  updateTraceMeasurementType: [traceId: number, measurementType: MeasurementType]
  updateSweep: [channelId: number, sweep: SweepSettings]
  updateSweepControl: [channelId: number, mode: SweepMode, sweepCount: number]
  restartSweep: [channelId: number]
  updateTraceFormat: [traceId: number, format: TraceFormat]
  updateTraceScalePerDivision: [traceId: number, value: number]
}>()

const channel = computed(() => props.state?.instrument.channels[0])
const { activeTraceId, activeTrace, activeMeasurement } = useActiveTrace(toRef(props, 'state'))
const isDiagramMaximized = ref(false)
const canMaximizeDiagram = computed(() => Boolean(activeTrace.value))
const workspace = computed(() => selectWorkspacePresentation({
  state: props.state,
  connection: props.connection,
  hasFrame: activeTrace.value ? props.display.lastComplete.has(activeTrace.value.id)
    || Boolean(props.display.currentPartial?.traces.has(activeTrace.value.id)) : false,
  displayError: props.displayError,
}))
// Nullable scale is the display-model capability seam; format must not re-derive support.
const activeScale = computed(() => activeTrace.value?.scale ?? null)
const activeSofttool = ref<'measurement' | 'format' | 'scale' | 'stimulus' | 'channel' | 'sweep' | null>(
  null,
)
const stimulusKey = ref<StimulusKey>('Start')
const channelKey = ref<ChannelKey>('Power / Bw / Avg')
const sweepPage = ref<SweepSofttoolPage>('control')
const activeKey = computed(() => {
  if (activeSofttool.value === 'measurement') return 'Meas'
  if (activeSofttool.value === 'format') return 'Format'
  if (activeSofttool.value === 'scale') return 'Scale'
  if (activeSofttool.value === 'stimulus') return stimulusKey.value
  if (activeSofttool.value === 'channel') return channelKey.value
  if (activeSofttool.value === 'sweep') return sweepPage.value === 'trigger' ? 'Trigger' : 'Sweep'
  return null
})
watch(activeTrace, (trace) => {
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
  if ((key === 'Sweep' || key === 'Trigger') && channel.value) {
    sweepPage.value = key === 'Trigger' ? 'trigger'
      : activeSofttool.value === 'sweep' ? nextSweepPage(sweepPage.value) : 'parameters'
    activeSofttool.value = 'sweep'
    return
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
function forwardSweepControl(channelId: number, mode: SweepMode, sweepCount: number): void {
  emit('updateSweepControl', channelId, mode, sweepCount)
}
function forwardRestartSweep(channelId: number): void {
  emit('restartSweep', channelId)
}
function forwardMeasurementTypeUpdate(measurementType: MeasurementType): void {
  if (activeTrace.value) emit('updateTraceMeasurementType', activeTrace.value.id, measurementType)
}
function forwardAllSParameters(): void {
  if (activeTrace.value) emit('ensureAllSParameters', activeTrace.value.id)
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
          :can-restart-sweep="Boolean(channel) && !workspace.controlsDisabled && !busy"
          :sweep-busy="busy"
          @toggle-maximize="toggleDiagramMaximized"
          @restart-sweep="channel && forwardRestartSweep(channel.id)"
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
          :frames="display.lastComplete"
          :partial="display.currentPartial"
          :active-trace-id="activeTrace?.id"
          :maximized="isDiagramMaximized"
          @select-trace="activeTraceId = $event"
        />
        <div v-if="workspace.mode === 'stale'" class="workspace-stale" role="status">
          {{ workspace.statusLabel }}
        </div>
        <div v-if="serviceError" class="workspace-stale command-error" role="alert">
          {{ serviceError }}
        </div>
      </div>

      <MeasurementSofttool
        v-if="activeSofttool === 'measurement'"
        :measurement-type="activeMeasurement?.type"
        :has-active-trace="Boolean(activeTrace)"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @ensure-all-s-parameters="forwardAllSParameters"
        @update-measurement-type="forwardMeasurementTypeUpdate"
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
      <SweepSofttool
        v-else-if="activeSofttool === 'sweep' && channel && state"
        :channel="channel"
        :page="sweepPage"
        :disabled="workspace.controlsDisabled"
        :busy="busy"
        @close="activeSofttool = null"
        @select-page="sweepPage = $event"
        @update-sweep="forwardSweepUpdate"
        @update-control="forwardSweepControl"
        @restart="forwardRestartSweep"
      />

      <HardkeyPanel
        :active-key="activeKey"
        :has-channel="Boolean(channel)"
        :has-trace="Boolean(activeTrace)"
        :has-scale="Boolean(activeScale)"
        @select="selectHardkey"
      />
    </div>

    <InstrumentStatusBar
      :state="state"
      :sweep-status="display.sweepStatus"
    />
  </section>
</template>
