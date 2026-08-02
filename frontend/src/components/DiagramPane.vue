<script setup lang="ts">
import { computed } from 'vue'
import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'
import type { MultiFormatTraceDisplayFrame } from '../api/traceDisplayFrameSet'
import CartesianAxisOverlay from './CartesianAxisOverlay.vue'
import CartesianCurve from './CartesianCurve.vue'
import SmithCurve from './SmithCurve.vue'
import SmithGrid from './SmithGrid.vue'
import {
  formatCartesianScaleSummary,
  selectCartesianAxis,
} from './cartesianAxisModel'
import { selectDiagramCurve } from './diagramCurveModel'
import { noMeasurementDataMessage } from './diagramModel'
import { traceColorForMeasurement } from './traceVisual'

const props = defineProps<{
  windowId: number
  kind: 'cartesian' | 'smith'
  channel?: ChannelSnapshot
  measurement?: MeasurementSnapshot
  trace?: TraceSnapshot
  frame?: MultiFormatTraceDisplayFrame
  active: boolean
}>()
const emit = defineEmits<{ select: [traceId: number] }>()
const traceColor = computed(() => traceColorForMeasurement(props.measurement?.type))

const traceLabel = computed(() => {
  if (!props.trace) return 'No active trace'
  return formatName(props.trace.format)
})
const axis = computed(() => selectCartesianAxis(props.trace))
const scaleSummary = computed(() => {
  if (props.trace?.format === 'smith') return '200 mU/ Ref 1 U'
  return axis.value ? formatCartesianScaleSummary(axis.value) : ''
})
const curve = computed(() => selectDiagramCurve(props.trace, props.measurement, props.frame))

function formatName(format: string): string {
  const names: Record<string, string> = {
    logMagnitude: 'dB Mag',
    phase: 'Phase',
    smith: 'Smith',
  }
  return names[format] ?? format
}

function frequency(value: number | undefined): string {
  if (value === undefined) return '—'
  if (value >= 1e9) return `${(value / 1e9).toFixed(1)} GHz`
  if (value >= 1e6) return `${(value / 1e6).toFixed(0)} MHz`
  if (value >= 1e3) return `${(value / 1e3).toFixed(0)} kHz`
  return `${value} Hz`
}

function selectTrace(): void {
  if (props.trace) emit('select', props.trace.id)
}
</script>

<template>
  <article
    class="diagram-pane"
    :style="{ '--trace-color': traceColor }"
    :aria-label="`Diagram ${windowId}${active ? ', active' : ''}`"
    :aria-current="active ? 'true' : undefined"
    :role="trace ? 'button' : undefined"
    :tabindex="trace ? 0 : undefined"
    @click="selectTrace"
    @keydown.enter="selectTrace"
    @keydown.space.prevent="selectTrace"
  >
    <header class="trace-strip" :class="{ active }">
      <span class="trace-index">{{ trace ? `Trc${trace.id}` : 'Trc —' }}</span>
      <span v-if="measurement" class="measurement-chip">{{ measurement.type }}</span>
      <span class="trace-name">{{ traceLabel }}</span>
      <span v-if="scaleSummary" class="trace-scale">{{ scaleSummary }}</span>
      <span class="strip-spacer" />
      <span
        class="diagram-identifier"
        :class="{ active }"
        aria-hidden="true"
      >
        <b>{{ windowId }}</b><span aria-hidden="true">▼</span>
      </span>
    </header>

    <div class="plot-area" :class="kind">
      <SmithGrid v-if="kind === 'smith'" />
      <CartesianAxisOverlay v-if="axis" :axis="axis" />
      <CartesianCurve
        v-if="curve?.kind === 'cartesian'"
        :trace-id="curve.traceId"
        :label="curve.label"
        :unit="curve.unit"
        :samples="curve.samples"
        :range="curve.range"
      />
      <SmithCurve
        v-else-if="curve?.kind === 'smith'"
        :trace-id="curve.traceId"
        :samples="curve.samples"
      />
      <span v-else class="plot-empty">{{ noMeasurementDataMessage }}</span>
    </div>

    <footer class="channel-row">
      <span class="channel-id" :class="{ active }">
        {{ channel ? `Ch${channel.id}` : 'No Ch' }}
      </span>
      <span>Start {{ frequency(channel?.sweep.startFrequencyHz) }}</span>
      <span>Pwr {{ channel ? `${channel.sweep.powerDbm} dBm` : '—' }}</span>
      <span>Bw {{ frequency(channel?.sweep.ifBandwidthHz) }}</span>
      <span>Stop {{ frequency(channel?.sweep.stopFrequencyHz) }}</span>
    </footer>
  </article>
</template>

<style scoped>
.diagram-pane { display: grid; grid-template-rows: 25px 1fr 23px; min-width: 0; min-height: 0; border: 1px solid #60717a; background: #050707; }
.trace-strip.active { background: #168fda; }
.trace-strip { display: flex; align-items: center; gap: 5px; padding: 0 5px; overflow: hidden; color: #dce5e8; background: #243138; font-size: 12px; white-space: nowrap; }
.trace-index { padding: 3px 5px; color: #dce5e8; background: transparent; }
.measurement-chip { padding: 2px 5px; color: #1b1b11; background: var(--trace-color); font-weight: 700; }
.trace-name { overflow: hidden; text-overflow: ellipsis; }
.trace-scale { color: #dce5e8; }
.strip-spacer { flex: 1; }
.diagram-identifier { align-self: stretch; display: flex; align-items: center; gap: 5px; min-width: 31px; margin-right: -5px; padding: 0 5px; background: #33454d; }
.diagram-identifier.active { background: #168fda; }
.diagram-identifier b { font-weight: 700; }
.plot-area { position: relative; overflow: hidden; background-color: #020404; color: #8ca0a8; }
.plot-area.cartesian { background-image: linear-gradient(#40515a 1px, transparent 1px), linear-gradient(90deg, #40515a 1px, transparent 1px); background-size: 100% 10%, 10% 100%; }
.plot-empty { position: absolute; inset: 0; display: grid; place-items: center; color: #536269; font-size: 12px; }
.channel-row { display: flex; align-items: center; justify-content: space-between; gap: 5px; padding: 0 4px; overflow: hidden; color: #d7e0e3; background: #202c32; font-size: 10px; white-space: nowrap; }
.channel-id { padding: 3px 4px; color: #fff; background: #2c3c43; font-weight: 700; }
.channel-id.active { background: #397cb4; }
</style>
