<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import {
  ensureAllSParameters,
  fetchState,
  setTraceMeasurementType,
  updateChannelSweep,
  updateTraceFormat,
  updateTraceScalePerDivision,
  type MeasurementType,
  type StateSnapshot,
  type SweepSettings,
  type TraceFormat,
} from './api/vnaApi'
import {
  removeDisplayFrame,
  replaceCompleteDisplayFramesForSnapshot,
  replaceDisplayFramesForSnapshot,
  retainDisplayFramesForSnapshot,
  type DisplayFrameSetMap,
} from './api/displayFrameSetState'
import {
  startLiveDisplaySession,
  type LiveDisplayConnection,
} from './api/liveDisplaySession'
import type { TraceDisplayFrameSet } from './api/traceDisplayFrameSet'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<LiveDisplayConnection>('connecting')
const serviceError = ref('')
const displayError = ref('')
const commandBusy = ref(false)
const frames = shallowRef<DisplayFrameSetMap>(new Map())
let pendingFrameTraceId: number | null = null
let pendingAllSParametersRevision: number | null = null
let stopLiveDisplay: (() => void) | null = null

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  const snapshot = await fetchState()
  frames.value = pendingAllSParametersRevision === null
    ? retainDisplayFramesForSnapshot(frames.value, snapshot)
    : new Map()
  state.value = snapshot
  // A successful authoritative refresh makes full frame identity sufficient again.
  pendingFrameTraceId = null
}

async function handleUpdateSweep(channelId: number, sweep: SweepSettings): Promise<void> {
  if (!state.value) return
  commandBusy.value = true
  try {
    await updateChannelSweep(state.value.stateRevision, channelId, sweep)
    await refreshState()
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleUpdateTraceFormat(traceId: number, format: TraceFormat): Promise<void> {
  if (!state.value) return
  commandBusy.value = true
  try {
    await updateTraceFormat(state.value.stateRevision, traceId, format)
    pendingFrameTraceId = traceId
    frames.value = removeDisplayFrame(frames.value, traceId)
    await refreshState()
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleUpdateTraceMeasurementType(
  traceId: number,
  measurementType: MeasurementType,
): Promise<void> {
  if (!state.value || commandBusy.value) return
  commandBusy.value = true
  try {
    await setTraceMeasurementType(state.value.stateRevision, traceId, measurementType)
    pendingFrameTraceId = traceId
    // Clear the accepted reconfiguration immediately; the next complete identity set may
    // repopulate it only after the authoritative state refresh establishes the new identity.
    frames.value = removeDisplayFrame(frames.value, traceId)
    // Only the refreshed snapshot may expose the new Measurement; no optimistic copy is created.
    await refreshState()
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleEnsureAllSParameters(traceId: number): Promise<void> {
  if (!state.value || commandBusy.value) return
  commandBusy.value = true
  try {
    const previousRevision = state.value.stateRevision
    const result = await ensureAllSParameters(previousRevision, traceId)
    // A changed revision invalidates the whole publication plan. A backend no-op keeps its
    // still-compatible FrameSet, while a real change waits on the next atomic four-Trace set.
    if (result.stateRevision !== previousRevision) {
      pendingAllSParametersRevision = result.stateRevision
      frames.value = new Map()
    }
    await refreshState()
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
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
    frames.value = complete
    pendingAllSParametersRevision = null
    displayError.value = ''
    return
  }
  let next = replaceDisplayFramesForSnapshot(frameSet, state.value)
  if (pendingFrameTraceId !== null) next = removeDisplayFrame(next, pendingFrameTraceId)
  frames.value = next
  displayError.value = ''
}

function handleConnectionChange(next: LiveDisplayConnection): void {
  connection.value = next
}

function handleDisplayError(error: Error): void {
  displayError.value = error.message
}

async function handleUpdateTraceScalePerDivision(traceId: number, value: number): Promise<void> {
  // Block before the first await so back-to-back submits cannot reuse one expected revision.
  if (!state.value || commandBusy.value) return
  commandBusy.value = true
  try {
    await updateTraceScalePerDivision(state.value.stateRevision, traceId, value)
    await refreshState()
    serviceError.value = ''
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

function openLiveDisplaySession(): void {
  stopLiveDisplay?.()
  stopLiveDisplay = startLiveDisplaySession(refreshState, {
    onFrameSet: replaceFrameSet,
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
        :frames="frames"
        @ensure-all-s-parameters="handleEnsureAllSParameters"
        @update-trace-measurement-type="handleUpdateTraceMeasurementType"
        @update-sweep="handleUpdateSweep"
        @update-trace-format="handleUpdateTraceFormat"
        @update-trace-scale-per-division="handleUpdateTraceScalePerDivision"
      />
    </div>
  </main>
</template>
