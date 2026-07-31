<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { checkHealth, fetchState, type StateSnapshot } from './api/vnaApi'
import ControlScreen from './components/ControlScreen.vue'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)
const state = ref<StateSnapshot | null>(null)
const connection = ref<'connecting' | 'online' | 'offline'>('connecting')
const serviceError = ref('')

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1760, window.innerHeight / 800)
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
      <MainScreen :state="state" :connection="connection" :service-error="serviceError" />
      <ControlScreen />
    </div>
  </main>
</template>
