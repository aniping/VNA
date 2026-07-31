<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import {
  checkHealth,
  createChannel,
  createMeasurement,
  createTrace,
  createWindow,
  fetchState,
  startSingleSweep,
  updateChannelSweep,
  updateTraceFormat,
  updateTraceScalePerDivision,
  type StateSnapshot,
  type SweepSettings,
  type TraceFormat,
  type TraceSetup,
} from './api/vnaApi'
import {
  fetchTraceDisplayFrame,
  type TraceDisplayFrame,
} from './api/traceDisplayFrameApi'
import { waitForTerminalOperation } from './api/operationApi'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<'connecting' | 'online' | 'offline'>('connecting')
const serviceError = ref('')
const commandBusy = ref(false)
const sweepBusy = ref(false)
const frames = shallowRef<ReadonlyMap<number, TraceDisplayFrame>>(new Map())
let sweepController: AbortController | null = null

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  try {
    await checkHealth()
    const snapshot = await fetchState()
    pruneFrames(snapshot)
    state.value = snapshot
    connection.value = 'online'
    serviceError.value = ''
  } catch (error) {
    connection.value = 'offline'
    serviceError.value = error instanceof Error ? error.message : 'Unknown error'
  }
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

function pruneFrames(snapshot: StateSnapshot): void {
  const validTraceIds = new Set(
    snapshot.instrument.traces
      .filter((trace) => trace.format === 'logMagnitude')
      .map((trace) => trace.id),
  )
  const retained = new Map(
    [...frames.value].filter(([traceId]) => validTraceIds.has(traceId)),
  )
  // App owns the only frame cache; pruning here prevents a removed or reformatted
  // Trace from regaining an old curve when a later UI selection reuses its pane.
  if (retained.size !== frames.value.size) frames.value = retained
}

function replaceFrame(frame: TraceDisplayFrame): void {
  const next = new Map(frames.value)
  next.set(frame.traceId, frame)
  frames.value = next
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

async function handleStartSingleSweep(channelId: number, traceId: number): Promise<void> {
  if (
    !state.value
    || connection.value !== 'online'
    || commandBusy.value
    || sweepController
  ) return
  const controller = new AbortController()
  sweepController = controller
  commandBusy.value = true
  sweepBusy.value = true
  serviceError.value = ''
  // Keep the last complete curve visible while work is pending. Nothing mutates
  // the Map until both the Operation and its identified frame have succeeded.
  try {
    const command = await startSingleSweep(
      state.value.stateRevision,
      channelId,
      controller.signal,
    )
    const operation = await waitForTerminalOperation(command.value.operationId, controller.signal)
    if (operation.status !== 'Succeeded') {
      throw new Error(`Sweep operation ${operation.status.toLowerCase()}`)
    }
    // A terminal Operation identifies the committed frame, so the display query is
    // issued exactly once and cannot make an old retained frame look newly complete.
    const frame = await fetchTraceDisplayFrame(traceId, controller.signal)
    if (!frame) throw new Error('Sweep completed without display data')
    if (frame.frameId !== operation.frameId || frame.traceId !== traceId) {
      throw new Error('Sweep frame identity mismatch')
    }
    const trace = state.value?.instrument.traces.find((item) => item.id === traceId)
    if (trace?.format !== 'logMagnitude') throw new Error('Sweep Trace is no longer displayable')
    replaceFrame(frame)
  } catch (error) {
    // Cancellation belongs to teardown; failures and the bounded client timeout remain visible.
    if (!controller.signal.aborted) {
      serviceError.value = error instanceof Error ? error.message : 'Sweep failed'
    }
  } finally {
    if (sweepController === controller) sweepController = null
    sweepBusy.value = false
    commandBusy.value = false
  }
}

onMounted(() => {
  resizeInstrument()
  window.addEventListener('resize', resizeInstrument)
  void refreshState()
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', resizeInstrument)
  sweepController?.abort()
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
        :sweep-busy="sweepBusy"
        :frames="frames"
        @create-channel="handleCreateChannel"
        @create-trace="handleCreateTrace"
        @update-sweep="handleUpdateSweep"
        @update-trace-format="handleUpdateTraceFormat"
        @update-trace-scale-per-division="handleUpdateTraceScalePerDivision"
        @start-single-sweep="handleStartSingleSweep"
      />
    </div>
  </main>
</template>
