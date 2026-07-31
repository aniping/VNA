<script setup lang="ts">
defineProps<{ activeKey: string | null }>()
const emit = defineEmits<{ select: [key: string] }>()

interface HardkeyGroup {
  title: string
  keys: Array<{ label: string; enabled?: boolean; accent?: 'help' | 'preset' }>
}

const groups: HardkeyGroup[] = [
  {
    title: 'Trace',
    keys: [
      { label: 'Meas', enabled: true },
      { label: 'Format' },
      { label: 'Scale' },
      { label: 'Trace Config' },
      { label: 'Line' },
      { label: 'Marker' },
    ],
  },
  {
    title: 'Stimulus',
    keys: [
      { label: 'Start', enabled: true },
      { label: 'Stop', enabled: true },
      { label: 'Center', enabled: true },
      { label: 'Span', enabled: true },
    ],
  },
  {
    title: 'Channel',
    keys: [
      { label: 'Power / Bw / Avg' },
      { label: 'Sweep' },
      { label: 'Cal' },
      { label: 'Channel Config' },
      { label: 'Mode' },
      { label: 'Offset / Embed' },
    ],
  },
  {
    title: 'System',
    keys: [
      { label: 'File / Print' },
      { label: 'Setup' },
      { label: 'Tools' },
      { label: 'Display' },
      { label: 'Help', accent: 'help' },
      { label: 'Preset', accent: 'preset' },
    ],
  },
]
</script>

<template>
  <nav class="hardkey-panel" aria-label="Virtual hard keys">
    <section v-for="group in groups" :key="group.title" class="hardkey-group">
      <h2>{{ group.title }}</h2>
      <div class="hardkey-grid">
        <button
          v-for="key in group.keys"
          :key="key.label"
          type="button"
          :class="[key.accent, { active: activeKey === key.label }]"
          :disabled="!key.enabled"
          @click="emit('select', key.label)"
        >
          {{ key.label }}
        </button>
      </div>
    </section>
  </nav>
</template>

<style scoped>
.hardkey-panel { min-width: 0; padding: 45px 5px 5px; overflow: hidden; background: #11191d; border-left: 2px solid #05090b; }
.hardkey-group { margin: 0 0 7px; }
.hardkey-group h2 { margin: 0 0 2px; color: #eef3f5; font-size: 12px; font-weight: 600; }
.hardkey-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 4px; }
button { min-height: 50px; padding: 3px 5px; border: 1px solid #11191e; border-radius: 1px; background: #53656e; color: #f7f9fa; font-size: 11px; line-height: 1.08; text-align: left; }
button.active { background: #408edf; }
button.help { color: #14110a; background: #e8a43f; text-align: center; }
button.preset { background: #438fa5; text-align: center; }
button:disabled { opacity: 1; cursor: default; }
</style>
