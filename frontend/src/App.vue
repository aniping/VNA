<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import {
  createChannel,
  createMeasurement,
  createTrace,
  createWindow,
  fetchState,
  updateChannelSweep,
  updateTraceFormat,
  updateTraceScalePerDivision,
  type StateSnapshot,
  type SweepSettings,
  type TraceFormat,
  type TraceSetup,
} from './api/vnaApi'
import {
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
const connection = ref<'connecting' | 'online' | 'offline'>('connecting')
const serviceError = ref('')
const commandBusy = ref(false)
const frames = shallowRef<ReadonlyMap<number, TraceDisplayFrame>>(new Map())
let stopLiveDisplay: (() => void) | null = null

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  const snapshot = await fetchState()
  frames.value = retainDisplayableFrames(frames.value, snapshot.instrument.traces)
  state.value = snapshot
  serviceError.value = ''
}

async function handleCreateChannel(sweep: SweepSettings): Promise<void> {
  if (!state.value) return
  commandBusy.value = true
  try {
    await createChannel(state.value.stateRevision, sweep)
    await refreshState()
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleCreateTrace(setup: TraceSetup): Promise<void> {
  const channel = state.value?.instrument.channels[0]
  if (!state.value || !channel) return
  commandBusy.value = true
  try {
    const measurement = await createMeasurement(
      state.value.stateRevision,
      channel.id,
      setup.measurementType,
    )
    const windowResult = await createWindow(measurement.stateRevision)
    await createTrace(
      windowResult.stateRevision,
      windowResult.value.windowId,
      measurement.value.measurementId,
      setup.format,
    )
    await refreshState()
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

async function handleUpdateSweep(channelId: number, sweep: SweepSettings): Promise<void> {
  if (!state.value) return
  commandBusy.value = true
  try {
    await updateChannelSweep(state.value.stateRevision, channelId, sweep)
    await refreshState()
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
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

function replaceFrame(frame: TraceDisplayFrame): void {
  const trace = state.value?.instrument.traces.find((item) => item.id === frame.traceId)
  if (trace?.format !== frame.format) return
  frames.value = replaceLatestDisplayFrame(frames.value, frame)
  serviceError.value = ''
}

function handleConnectionChange(next: LiveDisplayConnection): void {
  connection.value = next
  if (next === 'online') serviceError.value = ''
}

function handleDisplayError(error: Error): void {
  serviceError.value = error.message
}

async function handleUpdateTraceScalePerDivision(traceId: number, value: number): Promise<void> {
  // Block before the first await so back-to-back submits cannot reuse one expected revision.
  if (!state.value || commandBusy.value) return
  commandBusy.value = true
  try {
    await updateTraceScalePerDivision(state.value.stateRevision, traceId, value)
    await refreshState()
  } catch (error) {
    serviceError.value = error instanceof Error ? error.message : 'Command failed'
  } finally {
    commandBusy.value = false
  }
}

onMounted(() => {
  resizeInstrument()
  window.addEventListener('resize', resizeInstrument)
  // The session owns initial/reconnect state refresh and socket generation ordering.
  // App only owns the latest renderable frame per Trace and never starts acquisition.
  stopLiveDisplay = startLiveDisplaySession(refreshState, {
    onFrame: replaceFrame,
    onError: handleDisplayError,
    onConnectionChange: handleConnectionChange,
  })
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
        :disabled="connection !== 'online'"
        :busy="commandBusy"
        :frames="frames"
        @create-channel="handleCreateChannel"
        @create-trace="handleCreateTrace"
        @update-sweep="handleUpdateSweep"
        @update-trace-format="handleUpdateTraceFormat"
        @update-trace-scale-per-division="handleUpdateTraceScalePerDivision"
      />
    </div>
  </main>
</template>
