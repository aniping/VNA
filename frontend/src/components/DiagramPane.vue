<script setup lang="ts">
import { computed } from 'vue'
import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'
import type { TraceDisplayFrame } from '../api/traceDisplayFrameApi'
import CartesianCurve from './CartesianCurve.vue'
import { traceDisplayEmptyMessage } from './diagramModel'
import { traceColorForMeasurement } from './traceVisual'

const props = defineProps<{
  windowId: number
  kind: 'cartesian' | 'smith'
  channel?: ChannelSnapshot
  measurement?: MeasurementSnapshot
  trace?: TraceSnapshot
  frame?: TraceDisplayFrame
  active: boolean
}>()
const emit = defineEmits<{ select: [traceId: number] }>()
const traceColor = computed(() => traceColorForMeasurement(props.measurement?.type))

const traceLabel = computed(() => {
  if (!props.trace) return 'No active trace'
  return formatName(props.trace.format)
})
const scaleTop = computed(() => scaleBoundary('top'))
const scaleBottom = computed(() => scaleBoundary('bottom'))
const emptyMessage = computed(() => traceDisplayEmptyMessage(props.trace?.format))
const curve = computed(() => {
  const frame = props.frame
  const trace = props.trace
  const scale = trace?.scale
  // Identity and presentation checks prevent a parent from painting a stale Trace into this pane.
  if (props.kind !== 'cartesian' || !frame || !trace || !scale) return null
  if (frame.traceId !== trace.id || frame.format !== trace.format || frame.valueUnit !== scale.unit) {
    return null
  }
  return {
    traceId: frame.traceId,
    label: 'Log Magnitude',
    samples: { frequenciesHz: frame.frequenciesHz, values: frame.values },
    range: { minimum: scale.minimum, maximum: scale.maximum },
  }
})

function scaleBoundary(edge: 'top' | 'bottom'): string {
  // Smith labels describe circle geometry; only Cartesian labels consume display-model Scale.
  if (props.kind === 'smith') return edge === 'top' ? '1' : '0'
  const scale = props.trace?.scale
  if (!scale) return '—'
  const value = edge === 'top' ? scale.maximum : scale.minimum
  return `${value} ${scale.unit}`
}

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
      <div v-if="kind === 'smith'" class="smith-grid" aria-hidden="true">
        <span class="smith-ring ring-one" />
        <span class="smith-ring ring-two" />
        <span class="smith-ring ring-three" />
        <span class="smith-arc arc-top" />
        <span class="smith-arc arc-bottom" />
      </div>
      <span class="scale-top" :title="kind !== 'smith' && !trace?.scale ? 'Scale unavailable' : undefined">
        {{ scaleTop }}
      </span>
      <span class="scale-bottom" :title="kind !== 'smith' && !trace?.scale ? 'Scale unavailable' : undefined">
        {{ scaleBottom }}
      </span>
      <CartesianCurve
        v-if="curve"
        :trace-id="curve.traceId"
        :label="curve.label"
        :samples="curve.samples"
        :range="curve.range"
      />
      <span v-else class="plot-empty">{{ emptyMessage }}</span>
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
.strip-spacer { flex: 1; }
.diagram-identifier { align-self: stretch; display: flex; align-items: center; gap: 5px; min-width: 31px; margin-right: -5px; padding: 0 5px; background: #33454d; }
.diagram-identifier.active { background: #168fda; }
.diagram-identifier b { font-weight: 700; }
.plot-area { position: relative; overflow: hidden; background-color: #020404; color: #8ca0a8; }
.plot-area.cartesian { background-image: linear-gradient(#40515a 1px, transparent 1px), linear-gradient(90deg, #40515a 1px, transparent 1px), linear-gradient(#26343a 1px, transparent 1px), linear-gradient(90deg, #26343a 1px, transparent 1px); background-size: 100% 20%, 12.5% 100%, 100% 10%, 6.25% 100%; }
.scale-top, .scale-bottom { position: absolute; left: 4px; z-index: 2; font-size: 10px; }
.scale-top { top: 4px; }
.scale-bottom { bottom: 4px; }
.plot-empty { position: absolute; inset: 0; display: grid; place-items: center; color: #536269; font-size: 12px; }
.smith-grid { position: absolute; top: 50%; left: 50%; width: min(86%, 270px); aspect-ratio: 1; transform: translate(-50%, -50%); border: 1px solid #607580; border-radius: 50%; }
.smith-grid::before { content: ''; position: absolute; top: 50%; left: 0; width: 100%; border-top: 1px solid #607580; }
.smith-ring { position: absolute; top: 50%; right: 0; transform: translateY(-50%); aspect-ratio: 1; border: 1px solid #4a606a; border-radius: 50%; }
.ring-one { width: 75%; }
.ring-two { width: 50%; }
.ring-three { width: 25%; }
.smith-arc { position: absolute; right: 0; width: 73%; height: 73%; border: 1px solid #4a606a; border-radius: 50%; }
.arc-top { bottom: 50%; }
.arc-bottom { top: 50%; }
.channel-row { display: flex; align-items: center; justify-content: space-between; gap: 5px; padding: 0 4px; overflow: hidden; color: #d7e0e3; background: #202c32; font-size: 10px; white-space: nowrap; }
.channel-id { padding: 3px 4px; color: #fff; background: #2c3c43; font-weight: 700; }
.channel-id.active { background: #397cb4; }
</style>
