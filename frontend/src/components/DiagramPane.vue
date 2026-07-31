<script setup lang="ts">
import { computed } from 'vue'
import type {
  ChannelSnapshot,
  MeasurementSnapshot,
  TraceSnapshot,
} from '../api/vnaApi'

const props = defineProps<{
  paneNumber: number
  kind: 'cartesian' | 'smith'
  channel?: ChannelSnapshot
  measurement?: MeasurementSnapshot
  trace?: TraceSnapshot
  active: boolean
}>()
const emit = defineEmits<{ select: [traceId: number] }>()

const traceLabel = computed(() => {
  if (!props.trace) return 'No active trace'
  return formatName(props.trace.format)
})

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
    :class="{ active }"
    :aria-label="`Diagram ${paneNumber}`"
    :role="trace ? 'button' : undefined"
    :tabindex="trace ? 0 : undefined"
    @click="selectTrace"
    @keydown.enter="selectTrace"
    @keydown.space.prevent="selectTrace"
  >
    <header class="trace-strip">
      <span class="trace-index">{{ trace ? `Trc${trace.id}` : 'Trc —' }}</span>
      <span v-if="measurement" class="measurement-chip">{{ measurement.type }}</span>
      <span class="trace-name">{{ traceLabel }}</span>
      <span class="strip-spacer" />
      <span aria-hidden="true">▼</span>
    </header>

    <div class="plot-area" :class="kind">
      <div v-if="kind === 'smith'" class="smith-grid" aria-hidden="true">
        <span class="smith-ring ring-one" />
        <span class="smith-ring ring-two" />
        <span class="smith-ring ring-three" />
        <span class="smith-arc arc-top" />
        <span class="smith-arc arc-bottom" />
      </div>
      <span class="scale-top">{{ kind === 'smith' ? '1' : '10 dB' }}</span>
      <span class="scale-bottom">{{ kind === 'smith' ? '0' : '-90 dB' }}</span>
      <span class="plot-empty">No measurement data</span>
    </div>

    <footer class="channel-row">
      <span class="channel-id">{{ channel ? `Ch${channel.id}` : 'No Ch' }}</span>
      <span>Start {{ frequency(channel?.sweep.startFrequencyHz) }}</span>
      <span>Pwr {{ channel ? `${channel.sweep.powerDbm} dBm` : '—' }}</span>
      <span>Bw {{ frequency(channel?.sweep.ifBandwidthHz) }}</span>
      <span>Stop {{ frequency(channel?.sweep.stopFrequencyHz) }}</span>
    </footer>
  </article>
</template>

<style scoped>
.diagram-pane { display: grid; grid-template-rows: 25px 1fr 23px; min-width: 0; min-height: 0; border: 1px solid #60717a; background: #050707; }
.diagram-pane.active { border: 2px solid #f2db24; }
.diagram-pane.active .trace-strip { background: #168fda; box-shadow: inset 0 -3px #f2db24; }
.trace-strip { display: flex; align-items: center; gap: 5px; padding: 0 5px; overflow: hidden; color: #dce5e8; background: #243138; font-size: 12px; white-space: nowrap; }
.trace-index { padding: 3px 5px; color: #dce5e8; background: transparent; }
.measurement-chip { padding: 2px 5px; color: #1b1b11; background: #f2db24; font-weight: 700; }
.trace-name { overflow: hidden; text-overflow: ellipsis; }
.strip-spacer { flex: 1; }
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
.channel-id { padding: 3px 4px; color: #fff; background: #397cb4; font-weight: 700; }
</style>
