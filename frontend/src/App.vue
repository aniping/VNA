<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import {
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
  createLegacyFrameGuard,
  removeDisplayFrame,
  replaceLatestDisplayFrame,
  retainDisplayableFrames,
} from './api/displayFrameState'
import {
  startLiveDisplaySession,
  type LiveDisplayConnection,
} from './api/liveDisplaySession'
import type { TraceDisplayFrame } from './api/traceDisplayFrame'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<LiveDisplayConnection>('connecting')
const serviceError = ref('')
const displayError = ref('')
const commandBusy = ref(false)
const frames = shallowRef<ReadonlyMap<number, TraceDisplayFrame>>(new Map())
const legacyFrameGuard = createLegacyFrameGuard()
let stopLiveDisplay: (() => void) | null = null

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  const snapshot = await fetchState()
  frames.value = retainDisplayableFrames(frames.value, snapshot.instrument.traces)
  state.value = snapshot
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
    await refreshState()
    // A supported format starts a new socket generation after an intentional stream close.
    if (format === 'logMagnitude') openLiveDisplaySession()
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
    // A legacy frame cannot prove its Sij identity. Once configuration changes, this one-way
    // guard rejects that Trace until the full-identity frame-set transport replaces this path.
    legacyFrameGuard.block(traceId)
    // The legacy single-frame DTO has no Measurement identity. Clear only this accepted
    // reconfiguration so its prior Sij curve cannot masquerade as the pending new result.
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

function replaceFrame(frame: TraceDisplayFrame): void {
  if (!legacyFrameGuard.accepts(frame)) return
  const trace = state.value?.instrument.traces.find((item) => item.id === frame.traceId)
  if (trace?.format !== frame.format) return
  frames.value = replaceLatestDisplayFrame(frames.value, frame)
  displayError.value = ''
}

function handleConnectionChange(next: LiveDisplayConnection): void {
  connection.value = next
  if (next === 'online' || next === 'unavailable') {
    displayError.value = ''
  }
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
    onFrame: replaceFrame,
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
        @update-trace-measurement-type="handleUpdateTraceMeasurementType"
        @update-sweep="handleUpdateSweep"
        @update-trace-format="handleUpdateTraceFormat"
        @update-trace-scale-per-division="handleUpdateTraceScalePerDivision"
      />
    </div>
  </main>
</template>
