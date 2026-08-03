<script setup lang="ts">
import { computed } from 'vue'
import type { StateSnapshot } from '../api/vnaApi'
import type { CurrentSweepPartial } from '../api/displayFrameSetState'
import type { MultiFormatTraceDisplayFrame } from '../api/traceDisplayFrameSet'
import DiagramPane from './DiagramPane.vue'
import { selectDisplayDiagrams } from './diagramModel'

const props = defineProps<{
  state: StateSnapshot | null
  activeTraceId?: number
  maximized: boolean
  frames?: ReadonlyMap<number, MultiFormatTraceDisplayFrame>
  partial?: CurrentSweepPartial | null
}>()
const emit = defineEmits<{ selectTrace: [traceId: number] }>()
const diagrams = computed(() => selectDisplayDiagrams(props.state, props.activeTraceId))

function frameForTrace(traceId?: number): MultiFormatTraceDisplayFrame | undefined {
  const trace = props.state?.instrument.traces.find((item) => item.id === traceId)
  return trace ? props.frames?.get(trace.id) : undefined
}

function kindForTrace(format?: string): 'cartesian' | 'smith' {
  return format === 'smith' ? 'smith' : 'cartesian'
}
</script>

<template>
  <section
    class="diagram-grid"
    :class="{ maximized, single: diagrams.length === 1, double: diagrams.length === 2 }"
    aria-label="Measurement diagrams"
  >
    <DiagramPane
      v-for="diagram in diagrams"
      :key="diagram.windowId"
      :class="{
        'pane-hidden': maximized && !diagram.active,
      }"
      :window-id="diagram.windowId"
      :kind="kindForTrace(diagram.trace?.format)"
      :channel="diagram.channel"
      :trace="diagram.trace"
      :frame="frameForTrace(diagram.trace?.id)"
      :partial="partial"
      :measurement="diagram.measurement"
      :active="diagram.active"
      @select="emit('selectTrace', $event)"
    />
  </section>
</template>

<style scoped>
.diagram-grid { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); grid-template-rows: repeat(2, minmax(0, 1fr)); min-width: 0; min-height: 0; background: #050707; }
.diagram-grid.single { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
.diagram-grid.double { grid-template-rows: minmax(0, 1fr); }
.diagram-grid.maximized { grid-template-columns: minmax(0, 1fr); grid-template-rows: minmax(0, 1fr); }
.pane-hidden { display: none; }
</style>
