<script setup lang="ts">
import type { TraceFormat, TraceSnapshot } from '../api/vnaApi'

const props = defineProps<{
  trace: TraceSnapshot
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  updateFormat: [traceId: number, format: TraceFormat]
}>()

const formats: { label: string; value?: TraceFormat }[] = [
  { label: 'dB Mag', value: 'logMagnitude' },
  { label: 'Phase', value: 'phase' },
  { label: 'Smith', value: 'smith' },
  { label: 'Polar' },
  { label: 'SWR' },
  { label: 'Unwr Phase' },
  { label: 'Lin Mag' },
  { label: 'Log Mag' },
  { label: 'Real' },
  { label: 'Imag' },
  { label: 'Inv Smith' },
  { label: 'Delay' },
]

function formatName(format: string): string {
  return formats.find((item) => item.value === format)?.label ?? format
}

function selectFormat(format: TraceFormat | undefined): void {
  if (!format || props.disabled || props.busy || props.trace.format === format) return
  emit('updateFormat', props.trace.id, format)
}
</script>

<template>
  <aside class="format-softtool" aria-label="Trace format menu">
    <section class="format-column">
      <header class="format-summary">
        <span>Format</span>
        <span class="summary-caret">▼</span>
        <strong>{{ formatName(trace.format) }}</strong>
      </header>

      <div class="format-grid">
        <button
          v-for="format in formats"
          :key="format.label"
          type="button"
          :class="{ active: format.value === trace.format }"
          :disabled="disabled || busy || !format.value"
          @click="selectFormat(format.value)"
        >
          {{ format.label }}
        </button>
      </div>

      <button class="full-row" type="button" disabled>Delay Derivation</button>
      <div class="setting-row disabled-setting">
        <span>Aperture Points</span>
        <strong>10</strong>
      </div>
      <button class="marker-format" type="button" disabled>
        <span>Dflt Marker Frmt</span>
        <strong>Default</strong>
        <span>▼</span>
      </button>
    </section>

    <nav class="format-tab" aria-label="Trace format category">
      <button type="button" class="active">Format</button>
    </nav>
  </aside>
</template>

<style scoped>
.format-softtool { display: grid; grid-template-columns: 165px 1fr; min-width: 0; overflow: hidden; background: #10181c; border-left: 2px solid #05090b; }
.format-column { min-width: 0; padding: 3px; background: #11191d; }
.format-summary { display: grid; grid-template-columns: 1fr 24px; height: 46px; padding: 4px 5px; border: 1px solid #0b1114; background: #202c32; font-size: 11px; }
.format-summary strong { grid-column: 1 / 2; justify-self: end; font-weight: 500; }
.summary-caret { grid-column: 2; grid-row: 1 / 3; display: grid; place-items: center; background: #576a73; }
.format-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 3px; margin-top: 3px; }
.format-grid button, .full-row { min-height: 43px; padding: 4px 8px; border: 1px solid #0b1114; background: #202c32; font-size: 11px; text-align: left; }
.format-grid button.active { border-left: 4px solid #159ee3; background: #3e525c; }
.format-grid button:disabled, .full-row:disabled, .marker-format:disabled { color: #8e9ca2; opacity: 1; }
.full-row { width: 100%; margin-top: 3px; }
.setting-row { display: grid; min-height: 48px; margin-top: 3px; padding: 5px 7px; border: 1px solid #0b1114; background: #52656e; font-size: 11px; }
.setting-row strong { justify-self: end; font-weight: 500; }
.disabled-setting { color: #a9b4b9; }
.marker-format { display: grid; grid-template-columns: 1fr 22px; width: 100%; min-height: 48px; margin-top: 3px; padding: 4px 6px; border: 1px solid #0b1114; background: #202c32; font-size: 10px; text-align: left; }
.marker-format strong { justify-self: end; font-weight: 500; }
.marker-format > span:last-child { grid-column: 2; grid-row: 1 / 3; display: grid; place-items: center; background: #576a73; }
.format-tab { padding: 3px 2px; background: #11191d; }
.format-tab button { width: 100%; height: 46px; padding: 4px 7px; border: 1px solid #0b1114; background: #168fda; font-size: 11px; font-weight: 600; text-align: left; }
</style>
