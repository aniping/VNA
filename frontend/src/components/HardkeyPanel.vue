<script setup lang="ts">
import { hardkeyGroups, type HardkeyName } from './hardkeyModel'

defineProps<{
  activeKey: HardkeyName | null
  hasChannel: boolean
  hasTrace: boolean
  hasScale: boolean
}>()
const emit = defineEmits<{ select: [key: HardkeyName] }>()
</script>

<template>
  <nav class="hardkey-panel" aria-label="Virtual hard keys">
    <section v-for="group in hardkeyGroups" :key="group.title" class="hardkey-group">
      <h2>{{ group.title }}</h2>
      <div
        class="hardkey-grid"
        :style="{ '--hardkey-columns': group.columns }"
      >
        <button
          v-for="key in group.keys"
          :key="key.label"
          type="button"
          :class="[key.accent, { active: activeKey === key.label }]"
          :aria-label="key.visual ? key.label : undefined"
          :disabled="!key.enabled
            || (key.requiresChannel && !hasChannel)
            || (key.requiresTrace && !hasTrace)
            || (key.requiresScale && !hasScale)"
          @click="emit('select', key.label)"
        >
          <span v-if="key.visual === 'question'" class="help-symbol" aria-hidden="true">?</span>
          <template v-else>{{ key.label }}</template>
        </button>
      </div>
    </section>
  </nav>
</template>

<style scoped>
.hardkey-panel { min-width: 0; padding: 4px 5px 5px; overflow: hidden; background: #11191d; border-left: 2px solid #05090b; }
.hardkey-group { margin: 0 0 10px; }
.hardkey-group h2 { margin: 0 0 2px; color: #eef3f5; font-size: 12px; font-weight: 600; }
.hardkey-grid { display: grid; grid-template-columns: repeat(var(--hardkey-columns), minmax(0, 1fr)); gap: 4px; }
button { min-height: 50px; padding: 3px 5px; border: 1px solid #11191e; border-radius: 1px; background: #53656e; color: #f7f9fa; font-size: 11px; line-height: 1.08; text-align: left; }
button.active { background: #408edf; }
button.help { color: #14110a; background: #e8a43f; text-align: center; }
.help-symbol { display: inline-grid; place-items: center; width: 24px; height: 24px; border: 2px solid currentColor; border-radius: 50%; font-size: 16px; font-weight: 700; line-height: 1; }
button.preset { background: #438fa5; text-align: center; }
button:disabled { opacity: 1; cursor: default; }
</style>
