<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ChannelSnapshot, SweepMode, SweepSettings } from '../api/vnaApi'
import {
  parseSweepCount, parseSweepPoints, sweepControlUnavailableItems, sweepParameterRows,
  sweepSofttoolPages, sweepTypeItems, triggerInRows, triggerOutRows, triggerSourceItems,
  type SweepSofttoolPage,
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
      <button class="sweep-close" type="button" aria-label="Close Sweep" title="Close Sweep"
        @click="emit('close')">×</button>
    </header>
    <section class="sweep-page-content">
      <template v-if="page === 'control'">
        <fieldset class="radio-list">
          <legend class="sr-only">Sweep mode</legend>
          <label v-for="mode in sweepModes" :key="mode"
            class="radio-row" :class="{ selected: channel.sweepMode === mode }">
            <input type="radio" name="sweep-mode" :value="mode"
              :checked="channel.sweepMode === mode"
              :disabled="disabled || busy"
              @change="updateMode(mode)" />
            <span>{{ mode === 'continuous' ? 'Continuous' : 'Single' }}</span>
          </label>
        </fieldset>
        <label class="setting-row" for="sweep-count"><span>Sweeps</span>
          <input id="sweep-count" v-model="countDraft" inputmode="numeric"
            :disabled="disabled || busy || channel.sweepMode !== 'single'"
            @change="updateCount" @keydown.enter.prevent="updateCount" />
        </label>
        <button class="start-sweep" type="button" :disabled="disabled || busy"
          @click="emit('restart', channel.id)"><b aria-hidden="true">↻</b>Start Sweep</button>
        <button v-for="item in sweepControlUnavailableItems" :key="item"
          class="unavailable-row" type="button" disabled tabindex="-1"
          :aria-label="`${item}, not supported`" :title="`${item} — Not supported`">
          <b v-if="item === 'Restart Manager'" aria-hidden="true">⚙</b><span>{{ item }}</span>
          <em v-if="item === 'Sweep Controller' || item === 'Pipelining'">Off</em>
          <i v-if="item === 'Restart Manager' || item === 'Pipelining'">▶</i>
        </button>
      </template>
      <template v-else-if="page === 'trigger'">
        <fieldset class="radio-list">
          <legend class="sr-only">Trigger source</legend>
          <label v-for="source in triggerSourceItems" :key="source"
            class="radio-row" :class="{ selected: source === 'Free Run' }">
            <input type="radio" name="trigger-source" :value="source"
              :checked="source === 'Free Run'" disabled tabindex="-1" />
            <span>{{ source }}</span>
          </label>
        </fieldset>
        <button class="unavailable-row" type="button" disabled tabindex="-1">Manual Trigger</button>
        <button v-for="row in triggerInRows" :key="row.label"
          class="setting-row" type="button" disabled tabindex="-1">
          <span>{{ row.label }}</span><em>{{ row.value }}</em><i>▼</i>
        </button>
        <button class="unavailable-row" type="button" disabled tabindex="-1"
          aria-label="Trigger Manager, not supported"><b aria-hidden="true">⚙</b>
          <span>Trigger Manager</span><i>▶</i></button>
      </template>
      <template v-else-if="page === 'parameters'">
        <label class="setting-row active-setting" for="sweep-points">
          <span>Number of Points</span><input id="sweep-points" v-model="pointsDraft"
            inputmode="numeric" autofocus :disabled="disabled || busy"
            @change="updatePoints" @keydown.enter.prevent="updatePoints"
            @keydown.escape.prevent="restoreDrafts" />
        </label>
        <button v-for="row in sweepParameterRows" :key="row.label"
          class="setting-row" type="button" disabled tabindex="-1">
          <span>{{ row.label }}</span><em>{{ row.value }}</em>
        </button>
        <fieldset class="radio-list partial-list">
          <legend class="sr-only">Partial measurements</legend>
          <label class="radio-row"><input type="radio" disabled /><span>All Partial<br />Meas'ments</span></label>
          <label class="radio-row"><input type="radio" disabled /><span>First Partial<br />Meas'ment</span></label>
        </fieldset>
        <button class="setting-row" type="button" disabled tabindex="-1">
          <span>Freq Sweep Mode</span><em>—</em><i>▼</i></button>
      </template>
      <template v-else-if="page === 'type'">
        <fieldset class="radio-list">
          <legend class="sr-only">Sweep type</legend>
          <label v-for="item in sweepTypeItems" :key="item" class="radio-row">
            <input type="radio" :checked="item === 'Lin Freq'" disabled /><span>{{ item }}</span>
          </label>
        </fieldset>
        <button class="unavailable-row" type="button" disabled tabindex="-1">
          <b aria-hidden="true">⚙</b><span>Define Segments</span><i>▶</i></button>
        <button class="setting-row" type="button" disabled tabindex="-1">
          <span>Seg X-Axis</span><em>Freq based</em><i>▼</i></button>
      </template>
      <template v-else>
        <button v-for="row in triggerOutRows" :key="row.label"
          class="setting-row" type="button" disabled tabindex="-1">
          <span>{{ row.label }}</span><em>{{ row.value }}</em>
          <i v-if="row.label !== 'Trigger Out Active'">▼</i>
        </button>
      </template>
    </section>
    <nav class="sweep-pages" aria-label="Sweep pages">
      <button v-for="item in sweepSofttoolPages" :key="item.id" type="button"
        class="sweep-page" :class="{ selected: item.page === page }"
        :aria-selected="item.page === page" :title="item.label"
        @click="emit('selectPage', item.page)">{{ item.label }}</button>
    </nav>
  </aside>
</template>
<style scoped>
.sweep-softtool { display: grid; grid-template: 42px 1fr / minmax(0, 1fr) 84px;
  min-width: 0; overflow: hidden; background: #1c282e; border-left: 2px solid #05090b; }
.sweep-header { display: flex; align-items: center; gap: 7px; grid-column: 1 / -1;
  min-width: 0; padding: 5px 8px; background: #25333a; font-size: 11px; }
.sweep-close { width: 25px; height: 28px; margin-left: auto; border: 0; background: #53656e;
  font-size: 20px; line-height: 1; }
.sweep-page-content { min-width: 0; overflow: auto; background: #1c282e; }
.sweep-pages { display: flex; min-width: 0; padding-top: 5px; flex-direction: column;
  background: #11191d; }
.sweep-page { display: flex; align-items: center; width: 100%; min-height: 43px; padding: 6px;
  border: 1px solid #11191d; color: #f7f9fa; background: #53656e;
  font-size: 10px; line-height: 1.1; text-align: left; }
.sweep-page.selected { border-color: #082536; color: #fff; background: #168fda; }
.radio-list { display: grid; gap: 2px; margin: 5px; padding: 0; border: 0; }
.radio-row { display: flex; align-items: center; gap: 7px; min-height: 39px; padding: 0 7px;
  border: 1px solid #11191d; background: #26343b; font-size: 11px; }
.radio-row.selected { background: #26343b; }
.radio-row input { width: 14px; height: 14px; margin: 0; appearance: none;
  border: 2px solid #839197; border-radius: 50%; background: #26343b; }
.radio-row input:checked { background: #168fda; box-shadow: inset 0 0 0 3px #26343b; }
.setting-row, .start-sweep, .unavailable-row { display: flex; align-items: center; gap: 6px;
  width: calc(100% - 10px); min-height: 39px; margin: 2px 5px 0; padding: 5px 7px;
  border: 1px solid #11191d; background: #53656e; font-size: 10px; text-align: left; }
.setting-row { flex-wrap: wrap; justify-content: space-between; }
.setting-row span { width: 100%; }
.setting-row em, .unavailable-row em { margin-left: auto; font-style: normal; color: #f7f9fa; }
.setting-row i, .unavailable-row i { margin-left: 2px; font-style: normal; }
.setting-row input { width: 70px; margin-left: auto; padding: 2px 4px; color: #fff;
  border: 0; background: #05090b; text-align: right; }
.active-setting { border-bottom-color: #168fda; }
.start-sweep { background: #53656e; font-weight: 700; }
.start-sweep b, .unavailable-row b { font-size: 17px; line-height: 1; }
.partial-list { margin-top: 2px; margin-bottom: 2px; }
button:disabled { color: #839197; background: #35434a; cursor: default; }
.sr-only { position: absolute; width: 1px; height: 1px; overflow: hidden; clip: rect(0, 0, 0, 0); }
</style>
