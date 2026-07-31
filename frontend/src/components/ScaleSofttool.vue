<script setup lang="ts">
import { ref, watch } from 'vue'
import type { CartesianScaleSnapshot } from '../api/vnaApi'

const props = defineProps<{
  traceId: number
  scale: CartesianScaleSnapshot
  disabled: boolean
  busy: boolean
}>()
const emit = defineEmits<{
  updateScalePerDivision: [traceId: number, value: number]
}>()

const draft = ref('')
const invalid = ref(false)

function resetDraft(): void {
  draft.value = String(props.scale.scalePerDivision)
  invalid.value = false
}

function submit(): void {
  // Native Enter and form.requestSubmit both cross this sole command seam.
  if (props.disabled || props.busy) return
  const candidate = draft.value.trim()
  const value = Number(candidate)
  if (!candidate || !Number.isFinite(value) || value <= 0) {
    invalid.value = true
    return
  }
  emit('updateScalePerDivision', props.traceId, value)
}

function authorityValue(value: number, withUnit = true): string {
  return withUnit ? `${value} ${props.scale.unit}` : String(value)
}

watch(
  () => [props.traceId, props.scale.scalePerDivision],
  resetDraft,
  { immediate: true },
)
// Draft is UI-only: after any command lifecycle it must return to the authoritative snapshot.
watch(() => props.busy, (busy) => {
  if (!busy) resetDraft()
})
</script>

<template>
  <aside class="scale-softtool" aria-label="Scale values menu" :aria-busy="busy">
    <section class="scale-column">
      <button type="button" disabled title="Auto Scale Trace, not supported">
        Auto Scale Trace
      </button>
      <button type="button" disabled title="Auto Scale Diagram, not supported">
        Auto Scale Diagram
      </button>
      <button type="button" disabled title="Common Scale, not supported">
        <span>Auto Scale Diag.</span><small>(Common Scale)</small>
      </button>
      <button type="button" disabled title="Reference Value from Marker, not supported">
        Ref Value = Marker
      </button>

      <form class="scale-setting active" @submit.prevent="submit">
        <label for="scale-per-division">Scale/Div</label>
        <span class="value-entry">
          <input
            id="scale-per-division"
            v-model="draft"
            type="text"
            inputmode="decimal"
            autocomplete="off"
            spellcheck="false"
            autofocus
            :disabled="disabled || busy"
            :aria-invalid="invalid"
            :title="invalid ? 'Enter a finite value greater than zero' : 'Press Enter to apply'"
            @input="invalid = false"
            @blur="resetDraft"
            @keydown.esc.prevent="resetDraft"
          />
          <span>{{ scale.unit }}</span>
        </span>
      </form>

      <button type="button" disabled title="Reference Value editing, not supported">
        <span>Ref Value</span><strong>{{ authorityValue(scale.referenceValue) }}</strong>
      </button>
      <button type="button" disabled title="Reference Position editing, not supported">
        <span>Ref Pos</span><strong>{{ authorityValue(scale.referencePosition, false) }}</strong>
      </button>
      <button type="button" disabled title="Maximum editing, not supported">
        <span>Max</span><strong>{{ authorityValue(scale.maximum) }}</strong>
      </button>
      <button type="button" disabled title="Minimum editing, not supported">
        <span>Min</span><strong>{{ authorityValue(scale.minimum) }}</strong>
      </button>
      <button type="button" disabled title="Continuous Auto Scale, not supported">
        <span>Auto Scale Tr. Cont.</span><strong>—</strong>
      </button>
    </section>

    <nav class="scale-tab" aria-label="Scale category">
      <button type="button" class="active" aria-current="page">Scale Values</button>
    </nav>
  </aside>
</template>

<style scoped>
.scale-softtool { display: grid; grid-template-columns: 165px 1fr; min-width: 0; overflow: hidden; background: #10181c; border-left: 2px solid #05090b; }
.scale-column { min-width: 0; padding: 3px; background: #11191d; }
.scale-column > button, .scale-setting { display: grid; grid-template-columns: 1fr auto; align-items: center; width: 100%; min-height: 47px; margin-bottom: 3px; padding: 4px 7px; border: 1px solid #0b1114; background: #202c32; color: #d9e1e4; font-size: 11px; text-align: left; }
.scale-column > button:disabled { color: #8e9ca2; opacity: 1; }
.scale-column small { grid-column: 1; font-size: 9px; }
.scale-column strong { justify-self: end; font-weight: 500; }
.scale-setting.active { border-left: 4px solid #159ee3; background: #3e525c; }
.scale-setting:focus-within { background: #168fda; }
.value-entry { display: flex; align-items: center; justify-content: flex-end; gap: 3px; }
input { width: 70px; padding: 3px 4px; border: 1px solid #81949d; background: #10181c; color: #fff; font: inherit; text-align: right; }
input:focus { outline: 1px solid #fff; outline-offset: 0; }
input[aria-invalid='true'] { border-color: #ffb14a; }
input:disabled { color: #c1cbcf; opacity: 1; }
.scale-tab { padding: 3px 2px; background: #11191d; }
.scale-tab button { width: 100%; height: 46px; padding: 4px 7px; border: 1px solid #0b1114; background: #168fda; color: #fff; font-size: 11px; font-weight: 600; text-align: left; }
</style>
