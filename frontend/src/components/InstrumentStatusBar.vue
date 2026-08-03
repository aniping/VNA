<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import type { SweepStreamStatus } from '../api/sweepPreview'
import type { StateSnapshot } from '../api/vnaApi'
import { statusBarChannelLabel, statusBarSweepLabel } from './sweepSofttoolModel'

const props = defineProps<{
  state: StateSnapshot | null
  sweepStatus: SweepStreamStatus | null
}>()
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const channelLabel = computed(() => statusBarChannelLabel(channel.value))
const sweepLabel = computed(() => statusBarSweepLabel(
  props.sweepStatus, props.state?.sweepRuntime.phase,
))
const dateFormatter = new Intl.DateTimeFormat(undefined, {
  year: 'numeric', month: '2-digit', day: '2-digit',
  hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false,
})
const now = ref(new Date())
const localDateTime = computed(() => dateFormatter.format(now.value))
const localDateTimeIso = computed(() => now.value.toISOString())
let clock: number | undefined
// This timer only keeps the local clock current; sweep phase/progress always comes from the wire.
onMounted(() => { clock = window.setInterval(() => { now.value = new Date() }, 1000) })
onBeforeUnmount(() => { if (clock !== undefined) window.clearInterval(clock) })
</script>
<template>
  <nav class="menu-bar" aria-label="Application menu and instrument status">
    <button
      v-for="item in menus"
      :key="item"
      type="button"
      :aria-label="`${item}, not supported`"
      :title="`${item}, not supported`"
      disabled
    >{{ item }}</button>
    <span class="menu-spacer" />
    <span class="status-channel">{{ channelLabel }}</span>
    <span class="status-sweep">{{ sweepLabel }}</span>
    <time class="status-time" :datetime="localDateTimeIso">{{ localDateTime }}</time>
  </nav>
</template>
