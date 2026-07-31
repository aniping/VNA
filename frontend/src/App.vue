<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import {
  checkHealth,
  createChannel,
  createMeasurement,
  createTrace,
  createWindow,
  fetchState,
  type StateSnapshot,
  type SweepSettings,
  type TraceSetup,
} from './api/vnaApi'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<'connecting' | 'online' | 'offline'>('connecting')
const serviceError = ref('')
const commandBusy = ref(false)

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1280, window.innerHeight / 800)
}

async function refreshState(): Promise<void> {
  try {
    await checkHealth()
    state.value = await fetchState()
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

onMounted(() => {
  resizeInstrument()
  window.addEventListener('resize', resizeInstrument)
  void refreshState()
})

onBeforeUnmount(() => window.removeEventListener('resize', resizeInstrument))
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
        @create-channel="handleCreateChannel"
        @create-trace="handleCreateTrace"
      />
    </div>
  </main>
</template>
