<script setup lang="ts">
import ToolbarGlyph from './ToolbarGlyph.vue'

defineProps<{
  maximized: boolean
  canMaximize: boolean
}>()

const emit = defineEmits<{
  toggleMaximize: []
}>()

const groups = [
  { label: 'History', items: [
    { icon: 'undo', label: 'Undo' },
    { icon: 'redo', label: 'Redo' },
  ] },
  { label: 'Zoom', items: [
    { icon: 'zoom-area', label: 'Zoom Area' },
    { icon: 'zoom-one', label: 'Zoom 1:1' },
    { icon: 'zoom-settings', label: 'Zoom Settings' },
  ] },
  { label: 'Diagram View', items: [
    { icon: 'maximize', label: 'Maximize Diagram', action: 'maximize' },
  ] },
  { label: 'Trace and Marker', items: [
    { icon: 'add-trace', label: 'Add Trace' },
    { icon: 'add-marker', label: 'Add Marker' },
    { icon: 'delete', label: 'Delete Trace or Marker' },
  ] },
  { label: 'Helper Functions', items: [
    { icon: 'save-diagrams', label: 'Save Diagrams' },
    { icon: 'system-menu', label: 'Open System Menu' },
  ] },
  { label: 'Sweep', items: [
    { icon: 'restart-sweep', label: 'Restart Sweep', disabledReason: 'disabled during Continuous sweep' },
  ] },
  { label: 'Diagram Controls', items: [
    { icon: 'edit-diagrams', label: 'Edit Diagram Area' },
    { icon: 'other-measurements', label: 'Show Other Measurements' },
  ] },
  { label: 'Measurement Uncertainty', items: [
    { icon: 'uncertainty', label: 'Reconnect Measurement Uncertainty' },
  ] },
  { label: 'Automation', items: [
    { icon: 'scpi', label: 'SCPI Recorder' },
  ] },
  { label: 'Measurement Setup', items: [
    { icon: 'measurement-setup', label: 'Measurement Setup' },
  ] },
  { label: 'Source Modes', items: [
    { icon: 'source-coherence', label: 'Source Coherence' },
    { icon: 'pulse', label: 'Pulse Modulation' },
  ] },
  { label: 'RF Controls', items: [
    { icon: 'alc', label: 'ALC Configuration' },
    { icon: 'rf', label: 'RF On or Off' },
  ] },
] as const
</script>

<template>
  <header class="instrument-toolbar" role="toolbar" aria-label="Diagram toolbar">
    <template v-for="(group, groupIndex) in groups" :key="group.label">
      <span v-if="groupIndex === groups.length - 1" class="toolbar-flex-spacer" aria-hidden="true" />
      <span
        v-if="groupIndex > 0"
        class="toolbar-separator"
        role="separator"
        aria-orientation="vertical"
      />
      <div class="toolbar-group" role="group" :aria-label="group.label">
        <template v-for="item in group.items" :key="item.icon">
          <button
            v-if="'action' in item && item.action === 'maximize'"
            class="toolbar-button"
            type="button"
            data-toolbar-item="maximize"
            :disabled="!canMaximize"
            :aria-label="canMaximize ? item.label : `${item.label}, no active Trace`"
            :aria-pressed="canMaximize && maximized"
            :title="canMaximize ? item.label : `${item.label} — No active Trace`"
            @click="emit('toggleMaximize')"
            @keydown.enter.prevent="emit('toggleMaximize')"
            @keydown.space.prevent="emit('toggleMaximize')"
          >
            <ToolbarGlyph :name="item.icon" />
          </button>
          <button
            v-else
            class="toolbar-button"
            type="button"
            :data-toolbar-item="item.icon"
            disabled
            tabindex="-1"
            aria-disabled="true"
            :aria-label="`${item.label}, ${'disabledReason' in item
              ? item.disabledReason : 'not supported'}`"
            :title="`${item.label} — ${'disabledReason' in item
              ? item.disabledReason : 'Not supported'}`"
          >
            <ToolbarGlyph :name="item.icon" />
          </button>
        </template>
      </div>
    </template>
  </header>
</template>

<style scoped>
.instrument-toolbar { display: flex; align-items: center; width: 100%; height: 42px; padding: 3px; overflow: hidden; background: linear-gradient(#35434a, #202a2f); box-shadow: inset 0 -1px #0c1114; }
.toolbar-group { display: flex; flex: 0 0 auto; height: 36px; }
.toolbar-button { display: grid; place-items: center; flex: 0 0 36px; width: 36px; min-width: 36px; height: 36px; padding: 0; border: 0; border-radius: 0; color: #d7e0e3; background: transparent; }
.toolbar-button:not(:disabled):hover { background: #435158; }
.toolbar-button:not(:disabled):active, .toolbar-button[aria-pressed="true"] { background: #168fda; }
.toolbar-button:focus-visible { position: relative; z-index: 1; outline: 3px solid #ffd33d; outline-offset: -3px; }
.toolbar-button:disabled { color: #6f7c82; background: transparent; cursor: default; opacity: .72; }
.toolbar-separator { align-self: center; flex: 0 0 1px; width: 1px; height: 30px; margin: 0 3px; background: #62727a; }
.toolbar-flex-spacer { flex: 1 1 auto; min-width: 0; }
</style>
