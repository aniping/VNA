<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import ControlScreen from './components/ControlScreen.vue'
import MainScreen from './components/MainScreen.vue'

const scale = ref(1)

function resizeInstrument(): void {
  scale.value = Math.min(window.innerWidth / 1760, window.innerHeight / 800)
}

onMounted(() => {
  resizeInstrument()
  window.addEventListener('resize', resizeInstrument)
})

onBeforeUnmount(() => window.removeEventListener('resize', resizeInstrument))
</script>

<template>
  <main class="instrument-viewport">
    <div class="instrument-canvas" :style="{ transform: `scale(${scale})` }">
      <MainScreen />
      <ControlScreen />
    </div>
  </main>
</template>
