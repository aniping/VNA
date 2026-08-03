<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChannelSnapshot, SweepMode, SweepSettings } from '../api/vnaApi'
import {
  parseSweepCount, parseSweepPoints, sweepControlUnavailableItems, sweepSofttoolPages,
  triggerSourceItems, type SweepSofttoolPage,
} from './sweepSofttoolModel'

const props = defineProps<{
  channel: ChannelSnapshot
  page: SweepSofttoolPage
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  close: []
  selectPage: [page: SweepSofttoolPage]
  updateSweep: [channelId: number, sweep: SweepSettings]
  updateControl: [channelId: number, mode: SweepMode, sweepCount: number]
  restart: [channelId: number]
}>()
const pointsDraft = ref('')
const countDraft = ref('')
const points = computed(() => parseSweepPoints(pointsDraft.value))
const sweepCount = computed(() => parseSweepCount(countDraft.value))
const pageLabel = computed(() => sweepSofttoolPages.find(({ id }) => id === props.page)?.label)
const sweepModes: SweepMode[] = ['continuous', 'single']

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
  if (props.channel.sweepMode !== 'single') return
  emit('updateControl', props.channel.id, props.channel.sweepMode, sweepCount.value)
}
</script>
<template>
  <aside class="sweep-softtool" aria-label="Sweep menu">
    <header class="sweep-header">
      <strong>Sweep</strong>
      <span>{{ pageLabel }}</span>
      <button class="sweep-close" type="button" aria-label="Close Sweep" title="Close Sweep"
        @click="emit('close')">×</button>
    </header>
    <section class="sweep-page-content">
      <template v-if="page === 'control'">
        <h2>Sweep Control</h2>
        <fieldset class="radio-list">
          <legend class="sr-only">Sweep mode</legend>
          <label v-for="mode in sweepModes" :key="mode"
            class="radio-row" :class="{ selected: channel.sweepMode === mode }">
            <input type="radio" name="sweep-mode" :value="mode"
              :checked="channel.sweepMode === mode"
              :disabled="disabled || busy || channel.sweepMode === mode"
              @change="updateMode(mode)" />
            <span>{{ mode === 'continuous' ? 'Continuous' : 'Single' }}</span>
          </label>
        </fieldset>
        <label class="inline-value" for="sweep-count">Sweeps
          <input id="sweep-count" v-model="countDraft" inputmode="numeric"
            :disabled="disabled || busy || channel.sweepMode !== 'single'"
            @change="updateCount" />
        </label>
        <button class="start-sweep" type="button" :disabled="disabled || busy"
          @click="emit('restart', channel.id)">Start Sweep</button>
        <button v-for="item in sweepControlUnavailableItems" :key="item"
          class="unavailable-row" type="button" disabled tabindex="-1"
          :aria-label="`${item}, not supported`" :title="`${item} — Not supported`">{{ item }}</button>
      </template>
      <template v-else-if="page === 'trigger'">
        <h2>Trigger In</h2>
        <fieldset class="radio-list">
          <legend class="sr-only">Trigger source</legend>
          <label v-for="source in triggerSourceItems" :key="source"
            class="radio-row" :class="{ selected: source === 'Free Run' }">
            <input type="radio" name="trigger-source" :value="source"
              :checked="source === 'Free Run'" disabled tabindex="-1" />
            <span>{{ source }}</span>
          </label>
        </fieldset>
        <button class="unavailable-row" type="button" disabled tabindex="-1"
          aria-label="Trigger Manager, not supported">Trigger Manager</button>
      </template>
      <template v-else>
        <h2>Sweep Params</h2>
        <form @submit.prevent="updatePoints">
          <label for="sweep-points">Points</label>
          <div class="value-entry">
            <input id="sweep-points" v-model="pointsDraft" inputmode="numeric"
              :disabled="disabled || busy" @blur="restoreDrafts"
              @keydown.escape.prevent="restoreDrafts" />
            <span>pts</span>
          </div>
          <button type="submit" :disabled="disabled || busy || points === null
            || points === channel.sweep.points">Apply Points</button>
        </form>
      </template>
    </section>
    <nav class="sweep-pages" aria-label="Sweep pages">
      <button v-for="item in sweepSofttoolPages" :key="item.id" type="button"
        class="sweep-page" :class="{ selected: item.id === page }"
        :aria-selected="item.id === page" @click="emit('selectPage', item.id)">
        {{ item.label }}
      </button>
    </nav>
  </aside>
</template>
<style scoped>
.sweep-softtool { display: grid; grid-template: 42px 1fr / minmax(0, 1fr) 66px;
  min-width: 0; overflow: hidden; background: #1c282e; border-left: 2px solid #05090b; }
.sweep-header { display: flex; align-items: center; gap: 7px; grid-column: 1 / -1;
  min-width: 0; padding: 5px 8px; background: #25333a; font-size: 11px; }
.sweep-header span { overflow: hidden; color: #c5d0d4; text-overflow: ellipsis; white-space: nowrap; }
.sweep-close { width: 25px; height: 28px; margin-left: auto; border: 0; background: #53656e;
  font-size: 20px; line-height: 1; }
.sweep-page-content { min-width: 0; overflow: auto; background: #1c282e; }
.sweep-page-content h2 { margin: 0; padding: 8px; background: #11191d; font-size: 12px; }
.sweep-pages { display: flex; flex-direction: column; gap: 2px; min-width: 0; padding-top: 5px; background: #11191d; }
.sweep-page { min-height: 54px; padding: 5px; border: 1px solid #11191d; background: #53656e;
  color: #f7f9fa; font-size: 10px; line-height: 1.1; text-align: left; }
.sweep-page.selected { position: relative; z-index: 1; margin-left: -8px; border-color: #168fda; background: #168fda; }
.radio-list { display: grid; gap: 2px; margin: 5px; padding: 0; border: 0; }
.radio-row { display: flex; align-items: center; gap: 7px; min-height: 39px; padding: 0 7px;
  border: 1px solid #11191d; background: #53656e; font-size: 11px; }
.radio-row.selected { border-left: 4px solid #168fda; background: #65808d; }
.radio-row input { margin: 0; accent-color: #168fda; }
.inline-value { display: flex; align-items: center; justify-content: space-between; min-height: 39px;
  margin: 5px; padding: 0 7px; background: #53656e; font-size: 11px; }
.inline-value input { width: 66px; padding: 4px; color: #fff; border: 1px solid #71838c;
  background: #05090b; text-align: right; }
.start-sweep, .unavailable-row { width: calc(100% - 10px); min-height: 39px; margin: 2px 5px 0;
  padding: 5px 7px; border: 1px solid #11191d; background: #53656e; font-size: 10px; text-align: left; }
.start-sweep { border-color: #0b4258; background: #268fc5; font-weight: 700; }
form { margin: 10px 5px; padding: 8px; background: #11191d; }
form label { display: block; margin-bottom: 5px; font-size: 11px; }
.value-entry { display: grid; grid-template-columns: 1fr 45px; height: 38px; }
.value-entry input { min-width: 0; padding: 0 7px; color: #fff; border: 1px solid #71838c;
  background: #05090b; text-align: right; }
.value-entry span { display: grid; place-items: center; background: #53656e; font-size: 10px; }
form button { width: 100%; height: 38px; margin-top: 9px; border: 1px solid #0b4258;
  background: #268fc5; font-weight: 700; }
button:disabled { color: #839197; background: #35434a; cursor: default; }
button.selected:disabled { color: #fff; }
.sr-only { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0, 0, 0, 0); }
</style>
