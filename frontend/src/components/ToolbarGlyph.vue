<script setup lang="ts">
defineProps<{
  name:
    | 'undo' | 'redo' | 'zoom-area' | 'zoom-one' | 'zoom-settings'
    | 'maximize' | 'add-trace' | 'add-marker' | 'delete'
    | 'save-diagrams' | 'system-menu' | 'restart-sweep'
    | 'edit-diagrams' | 'other-measurements' | 'uncertainty' | 'scpi'
    | 'measurement-setup' | 'source-coherence' | 'pulse' | 'alc' | 'rf'
}>()
</script>

<template>
  <svg viewBox="0 0 24 24" aria-hidden="true" focusable="false">
    <template v-if="name === 'undo'">
      <path d="M9 7H4V2" />
      <path d="M4 7c2.3-2.6 5.2-3.8 8.1-3.1a8 8 0 0 1 6 8.9 7.5 7.5 0 0 1-7.4 6.4" />
    </template>
    <template v-else-if="name === 'redo'">
      <path d="M15 7h5V2" />
      <path d="M20 7c-2.3-2.6-5.2-3.8-8.1-3.1a8 8 0 0 0-6 8.9 7.5 7.5 0 0 0 7.4 6.4" />
    </template>
    <template v-else-if="name === 'zoom-area'">
      <rect class="dashed" x="2.5" y="2.5" width="12" height="12" />
      <circle cx="14.5" cy="14.5" r="4.5" />
      <path d="m18 18 3.5 3.5" />
    </template>
    <template v-else-if="name === 'zoom-one'">
      <circle cx="10" cy="10" r="7" />
      <path d="m15.2 15.2 5 5" />
      <text class="icon-text tiny" x="10" y="11.5">1:1</text>
    </template>
    <template v-else-if="name === 'zoom-settings'">
      <circle cx="9" cy="9" r="5.5" />
      <path d="m13 13 3.5 3.5M18.5 15.5v6M15.5 18.5h6" />
    </template>
    <template v-else-if="name === 'maximize'">
      <path d="M9 3H3v6M15 3h6v6M9 21H3v-6M15 21h6v-6" />
      <path d="m3 3 6 6m12-6-6 6M3 21l6-6m12 6-6-6" />
    </template>
    <template v-else-if="name === 'add-trace'">
      <path d="M2 16h4l3-8 4 10 3-6" />
      <path d="M19 14v8M15 18h8" />
    </template>
    <template v-else-if="name === 'add-marker'">
      <path d="m3 4 6 6-6 6M3 10h9" />
      <path d="M18 13v8M14 17h8" />
    </template>
    <template v-else-if="name === 'delete'">
      <path d="M4 7h16M9 3h6l1 4M7 7l1 14h8l1-14M10 11v6M14 11v6" />
    </template>
    <template v-else-if="name === 'save-diagrams'">
      <path d="M3 7h4l2-3h6l2 3h4v13H3z" />
      <circle cx="12" cy="13" r="4" />
    </template>
    <template v-else-if="name === 'system-menu'">
      <rect x="3" y="3" width="7" height="7" />
      <rect x="14" y="3" width="7" height="7" />
      <rect x="3" y="14" width="7" height="7" />
      <rect x="14" y="14" width="7" height="7" />
    </template>
    <template v-else-if="name === 'restart-sweep'">
      <path d="M18.5 7.5V3l3 3-3 3V7.5A8 8 0 1 0 20 15" />
    </template>
    <template v-else-if="name === 'edit-diagrams'">
      <rect x="3" y="3" width="18" height="18" />
      <path d="M12 3v18M3 12h18M16 8l5-5M17 3h4v4" />
    </template>
    <template v-else-if="name === 'other-measurements'">
      <path d="M2 9c3-6 5 6 8 0s5 6 8 0 3-1 4 0" />
      <path d="M2 16c3-6 5 6 8 0s5 6 8 0 3-1 4 0" />
    </template>
    <template v-else-if="name === 'uncertainty'">
      <path d="M3 8h16m0 0-4-4m4 4-4 4M21 16H5m0 0 4-4m-4 4 4 4" />
    </template>
    <text v-else-if="name === 'scpi'" class="icon-text" x="12" y="14">SCPI</text>
    <template v-else-if="name === 'measurement-setup'">
      <circle cx="12" cy="12" r="3.5" />
      <path d="M12 2v4m0 12v4M2 12h4m12 0h4M5 5l3 3m8 8 3 3M19 5l-3 3M8 16l-3 3" />
    </template>
    <template v-else-if="name === 'source-coherence'">
      <path d="M2 8h4m-4 8h4M8 6c3 0 3 4 6 4s3-4 6-4M8 14c3 0 3 4 6 4s3-4 6-4" />
    </template>
    <template v-else-if="name === 'pulse'">
      <path d="M2 17V8h5v8h5V8h5v8h5V7" />
    </template>
    <text v-else-if="name === 'alc'" class="icon-text" x="12" y="14">ALC</text>
    <template v-else>
      <text class="icon-text" x="9" y="14">RF</text>
      <path d="M17 5v6M14.5 7.5a5 5 0 1 0 5 0" />
    </template>
  </svg>
</template>

<style scoped>
svg { width: 22px; height: 22px; fill: none; stroke: currentColor; stroke-width: 1.75; stroke-linecap: round; stroke-linejoin: round; }
.dashed { stroke-dasharray: 2 2; }
.icon-text { fill: currentColor; stroke: none; font: 700 5px "Segoe UI", sans-serif; text-anchor: middle; }
.icon-text.tiny { font-size: 4px; }
</style>
