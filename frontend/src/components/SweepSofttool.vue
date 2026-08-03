<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type {
  ChannelSnapshot, SweepMode, SweepRuntimeSnapshot, SweepSettings,
} from '../api/vnaApi'
import {
  parseSweepCount, parseSweepPoints, sweepExecutionLabel, sweepSofttoolPages,
  type SweepSofttoolPage,
} from './sweepSofttoolModel'
const props = defineProps<{
  channel: ChannelSnapshot
  runtime: SweepRuntimeSnapshot
  page: SweepSofttoolPage
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  selectPage: [page: SweepSofttoolPage]
  updateSweep: [channelId: number, sweep: SweepSettings]
  updateControl: [channelId: number, mode: SweepMode, sweepCount: number]
  restart: [channelId: number]
}>()
const pointsDraft = ref('')
const countDraft = ref('')
const points = computed(() => parseSweepPoints(pointsDraft.value))
const sweepCount = computed(() => parseSweepCount(countDraft.value))
function restoreDrafts(): void {
  pointsDraft.value = String(props.channel.sweep.points)
  countDraft.value = String(props.channel.sweepCount)
}
watch(() => [props.channel.sweep.points, props.channel.sweepCount], restoreDrafts,
  { immediate: true })
function updatePoints(): void {
  if (points.value === null || points.value === props.channel.sweep.points) return
  emit('updateSweep', props.channel.id, { ...props.channel.sweep, points: points.value })
}
function updateMode(mode: SweepMode): void {
  if (mode === props.channel.sweepMode) return
  emit('updateControl', props.channel.id, mode, props.channel.sweepCount)
}
function updateCount(): void {
  if (sweepCount.value === null || sweepCount.value === props.channel.sweepCount) return
  emit('updateControl', props.channel.id, props.channel.sweepMode, sweepCount.value)
}
</script>
<template>
  <aside class="sweep-softtool" aria-label="Sweep menu">
    <header><strong>Sweep Menu</strong><span>{{ sweepSofttoolPages.find(({ id }) => id === page)?.label }}</span></header>
    <nav aria-label="Sweep pages">
      <button
        v-for="item in sweepSofttoolPages"
        :key="item.id"
        type="button"
        :class="{ selected: item.id === page }"
        :aria-pressed="item.id === page"
        @click="emit('selectPage', item.id)"
      >{{ item.label }}</button>
    </nav>
    <template v-if="page === 'parameters'">
      <h2>Sweep Parameters</h2>
      <form @submit.prevent="updatePoints">
        <label for="sweep-points">Points</label>
        <div class="value-entry">
          <input
            id="sweep-points"
            v-model="pointsDraft"
            inputmode="numeric"
            :disabled="disabled || busy"
            @blur="restoreDrafts"
            @keydown.escape.prevent="restoreDrafts"
          />
          <span>pts</span>
        </div>
        <button type="submit" :disabled="disabled || busy || points === null
          || points === channel.sweep.points">Apply Points</button>
      </form>
    </template>
    <template v-else-if="page === 'trigger'">
      <h2>Trigger In</h2>
      <button class="choice selected" type="button" disabled aria-pressed="true">None</button>
      <button class="choice" type="button" disabled>External</button>
      <button class="choice" type="button" disabled>Manual</button>
      <button class="choice" type="button" disabled>Multiple</button>
    </template>
    <template v-else>
      <h2>Sweep Control</h2>
      <div class="mode-buttons">
        <button
          type="button"
          :class="{ selected: channel.sweepMode === 'continuous' }"
          :aria-pressed="channel.sweepMode === 'continuous'"
          :disabled="disabled || busy || channel.sweepMode === 'continuous'"
          @click="updateMode('continuous')"
        >Continuous</button>
        <button
          type="button"
          :class="{ selected: channel.sweepMode === 'single' }"
          :aria-pressed="channel.sweepMode === 'single'"
          :disabled="disabled || busy || channel.sweepMode === 'single'"
          @click="updateMode('single')"
        >Single</button>
      </div>
      <form @submit.prevent="updateCount">
        <label for="sweep-count">Sweeps</label>
        <div class="value-entry">
          <input
            id="sweep-count"
            v-model="countDraft"
            inputmode="numeric"
            :disabled="disabled || busy || channel.sweepMode !== 'single'"
            @blur="restoreDrafts"
            @keydown.escape.prevent="restoreDrafts"
          />
          <span>×</span>
        </div>
        <button type="submit" :disabled="disabled || busy || channel.sweepMode !== 'single'
          || sweepCount === null || sweepCount === channel.sweepCount">Apply Sweeps</button>
      </form>
      <button class="restart" type="button" :disabled="disabled || busy"
        @click="emit('restart', channel.id)">Restart Sweep</button>
      <p>{{ sweepExecutionLabel('Configured', runtime.configured) }}</p>
      <p>{{ sweepExecutionLabel('Applied', runtime.applied) }}</p>
    </template>
  </aside>
</template>
<style scoped>
.sweep-softtool { min-width: 0; overflow: hidden; background: #1c282e; border-left: 2px solid #05090b; }
header { display: grid; height: 42px; padding: 5px 8px; background: #25333a; font-size: 11px; }
header span { justify-self: end; }
h2 { margin: 0; padding: 8px; background: #11191d; font-size: 12px; }
nav, .mode-buttons { display: grid; grid-template-columns: repeat(3, 1fr); gap: 2px; padding: 3px; }
nav button, .choice, .mode-buttons button { min-height: 43px; border: 1px solid #11191d; background: #53656e; text-align: left; }
button.selected { background: #168fda; }
button:disabled { color: #87959b; cursor: default; }
button.selected:disabled { color: #fff; }
.choice { width: calc(50% - 5px); margin: 3px 0 0 3px; }
form { margin: 10px 5px; padding: 8px; background: #11191d; }
label { display: block; margin-bottom: 5px; font-size: 11px; }
.value-entry { display: grid; grid-template-columns: 1fr 45px; height: 38px; }
input { min-width: 0; padding: 0 7px; color: #fff; border: 1px solid #71838c; background: #05090b; text-align: right; }
.value-entry span { display: grid; place-items: center; background: #53656e; font-size: 10px; }
form button, .restart { width: 100%; height: 38px; margin-top: 9px; border: 1px solid #0b4258; background: #268fc5; font-weight: 700; }
.restart { width: calc(100% - 10px); margin: 0 5px; }
form button:disabled, .restart:disabled { color: #839197; background: #35434a; }
p { margin: 7px 8px; color: #c7d1d5; font-size: 10px; }
</style>
