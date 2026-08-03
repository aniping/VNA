<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import {
  ensureAllSParameters,
  fetchState,
  setTraceMeasurementType,
  startSingleSweep,
  updateChannelSweep,
  updateChannelSweepControl,
  updateTraceFormat,
  updateTraceScalePerDivision,
  type MeasurementType,
  type StateSnapshot,
  type SweepMode,
  type SweepSettings,
  type TraceFormat,
} from './api/vnaApi'
import {
  acceptCompleteFrameSet,
  acceptSweepPreviewEvent,
  clearLiveDisplayData,
  emptyLiveDisplayState,
  removeLiveDisplayTrace,
  replaceCompleteDisplayFramesForSnapshot,
  retainLiveDisplayForSnapshot,
} from './api/displayFrameSetState'
import { sweepBoundaryKey } from './components/sweepSofttoolModel'
import {
  startLiveDisplaySession,
  type LiveDisplayConnection,
} from './api/liveDisplaySession'
import type { TraceDisplayFrameSet } from './api/traceDisplayFrameSet'
import type { SweepPreviewEvent } from './api/sweepPreview'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<LiveDisplayConnection>('connecting')
const serviceError = ref('')
const displayError = ref('')
const commandBusy = ref(false)
const display = shallowRef(emptyLiveDisplayState())
let pendingFrameTraceId: number | null = null
let pendingAllSParametersRevision: number | null = null
let refreshedSweepBoundaryKey: string | null = null
let stopLiveDisplay: (() => void) | null = null

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  const snapshot = await fetchState()
  display.value = pendingAllSParametersRevision === null
    ? retainLiveDisplayForSnapshot(display.value, snapshot)
    : clearLiveDisplayData(display.value)
  state.value = snapshot
  // A successful authoritative refresh makes full frame identity sufficient again.
  pendingFrameTraceId = null
}

async function runCommand(action: (snapshot: StateSnapshot) => Promise<void>): Promise<void> {
  const snapshot = state.value
  // Claim busy synchronously so two UI events cannot reuse one expected revision.
  if (!snapshot || commandBusy.value) return
  commandBusy.value = true
  try {
    await action(snapshot)
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleUpdateSweep(channelId: number, sweep: SweepSettings): Promise<void> {
  await runCommand(async (snapshot) => {
    await updateChannelSweep(snapshot.stateRevision, channelId, sweep)
    const previous = snapshot.instrument.channels.find(({ id }) => id === channelId)?.sweep
    const axisChanged = !previous || previous.startFrequencyHz !== sweep.startFrequencyHz
      || previous.stopFrequencyHz !== sweep.stopFrequencyHz || previous.points !== sweep.points
    if (axisChanged) display.value = clearLiveDisplayData(display.value)
    await refreshState()
  })
}

async function handleUpdateTraceFormat(traceId: number, format: TraceFormat): Promise<void> {
  await runCommand(async (snapshot) => {
    await updateTraceFormat(snapshot.stateRevision, traceId, format)
    pendingFrameTraceId = traceId
    display.value = removeLiveDisplayTrace(display.value, traceId)
    await refreshState()
  })
}

async function handleUpdateTraceMeasurementType(
  traceId: number,
  measurementType: MeasurementType,
): Promise<void> {
  await runCommand(async (snapshot) => {
    await setTraceMeasurementType(snapshot.stateRevision, traceId, measurementType)
    pendingFrameTraceId = traceId
    // Clear the accepted reconfiguration immediately; the next complete identity set may
    // repopulate it only after the authoritative state refresh establishes the new identity.
    display.value = removeLiveDisplayTrace(display.value, traceId)
    // Only the refreshed snapshot may expose the new Measurement; no optimistic copy is created.
    await refreshState()
  })
}

async function handleEnsureAllSParameters(traceId: number): Promise<void> {
  await runCommand(async (snapshot) => {
    const previousRevision = snapshot.stateRevision
    const result = await ensureAllSParameters(previousRevision, traceId)
    // A changed revision invalidates the whole publication plan. A backend no-op keeps its
    // still-compatible FrameSet, while a real change waits on the next atomic four-Trace set.
    if (result.stateRevision !== previousRevision) {
      pendingAllSParametersRevision = result.stateRevision
      display.value = clearLiveDisplayData(display.value)
    }
    await refreshState()
  })
}

function replaceFrameSet(frameSet: TraceDisplayFrameSet): void {
  if (!state.value) return
  if (pendingAllSParametersRevision !== null) {
    if (state.value.stateRevision < pendingAllSParametersRevision) return
    const minimumRevision = pendingAllSParametersRevision
    const complete = replaceCompleteDisplayFramesForSnapshot(
      frameSet,
      state.value,
      minimumRevision,
    )
    if (!complete) return
    display.value = {
      ...display.value,
      generation: frameSet.generation,
      lastComplete: complete,
      currentPartial: null,
    }
    pendingAllSParametersRevision = null
    displayError.value = ''
    return
  }
  let next = acceptCompleteFrameSet(display.value, frameSet, state.value)
  if (pendingFrameTraceId !== null) next = removeLiveDisplayTrace(next, pendingFrameTraceId)
  display.value = next
  displayError.value = ''
}

function replacePreview(event: SweepPreviewEvent): void {
  if (!state.value) return
  const previousPhase = display.value.sweepStatus?.userPhase
  display.value = acceptSweepPreviewEvent(display.value, event, state.value)
  if (event.type === 'available') displayError.value = ''
  const key = sweepBoundaryKey(event.sweepStatus)
  if (state.value.sweepRuntime.configured.mode === 'single'
    && key && event.sweepStatus.generation === display.value.generation
    && previousPhase !== null && previousPhase !== 'hold'
    && refreshedSweepBoundaryKey !== key) {
    refreshedSweepBoundaryKey = key
    void refreshState().catch(handleDisplayError)
  }
}

function handleConnectionChange(next: LiveDisplayConnection): void {
  connection.value = next
}

function handleDisplayError(error: Error): void {
  displayError.value = error.message
}

async function handleUpdateTraceScalePerDivision(traceId: number, value: number): Promise<void> {
  await runCommand(async (snapshot) => {
    await updateTraceScalePerDivision(snapshot.stateRevision, traceId, value)
    await refreshState()
  })
}

async function handleUpdateSweepControl(
  channelId: number,
  mode: SweepMode,
  sweepCount: number,
): Promise<void> {
  await runCommand(async (snapshot) => {
    await updateChannelSweepControl(snapshot.stateRevision, channelId, mode, sweepCount)
    await refreshState()
  })
}

async function handleRestartSweep(channelId: number): Promise<void> {
  await runCommand(async (snapshot) => {
    await startSingleSweep(snapshot.stateRevision, channelId)
    await refreshState()
  })
}

function openLiveDisplaySession(): void {
  stopLiveDisplay?.()
  stopLiveDisplay = startLiveDisplaySession(refreshState, {
    onFrameSet: replaceFrameSet,
    onPreviewEvent: replacePreview,
    onError: handleDisplayError,
    onConnectionChange: handleConnectionChange,
  })
}

onMounted(() => {
  resizeInstrument()
  window.addEventListener('resize', resizeInstrument)
  // The session owns initial/reconnect state refresh and socket generation ordering.
  // App only owns the latest renderable frame per Trace and never starts acquisition.
  openLiveDisplaySession()
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', resizeInstrument)
  stopLiveDisplay?.()
  stopLiveDisplay = null
})
</script>

<template>
  <main class="instrument-viewport">
    <div class="instrument-canvas" :style="{ transform: `scale(${scale})` }">
      <MainScreen
        :state="state"
        :connection="connection"
        :service-error="serviceError"
        :display-error="displayError"
        :busy="commandBusy"
        :display="display"
        @ensure-all-s-parameters="handleEnsureAllSParameters"
        @update-trace-measurement-type="handleUpdateTraceMeasurementType"
        @update-sweep="handleUpdateSweep"
        @update-sweep-control="handleUpdateSweepControl"
        @restart-sweep="handleRestartSweep"
        @update-trace-format="handleUpdateTraceFormat"
        @update-trace-scale-per-division="handleUpdateTraceScalePerDivision"
      />
    </div>
  </main>
</template>
