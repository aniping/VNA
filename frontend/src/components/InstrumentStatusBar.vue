<script setup lang="ts">
import { computed } from 'vue'
import type { SweepStreamStatus } from '../api/sweepPreview'
import type { StateSnapshot } from '../api/vnaApi'
import type { WorkspacePresentation } from './workspacePresentation'
import { sweepModeLabel, sweepPhaseLabel, sweepProgressLabel } from './sweepSofttoolModel'
const props = defineProps<{
  state: StateSnapshot | null
  workspace: WorkspacePresentation
  serviceError: string
  displayError: string
  sweepStatus: SweepStreamStatus | null
}>()
const menus = ['File', 'Trace', 'Channel', 'Display', 'Tools', 'System', 'Help']
const channel = computed(() => props.state?.instrument.channels[0])
const entityCounts = computed(() => {
  const instrument = props.state?.instrument
  if (!instrument) return 'Ch — · Meas — · Trc — · Win —'
  return `Ch ${instrument.channels.length} · Meas ${instrument.measurements.length}`
    + ` · Trc ${instrument.traces.length} · Win ${instrument.windows.length}`
})
const acquisitionStatus = computed(() => {
  if (!channel.value) return 'Sweep — · Trigger —'
  const mode = sweepModeLabel(channel.value.sweepMode)
  const progress = props.sweepStatus ? sweepProgressLabel(props.sweepStatus)
    : props.state ? sweepPhaseLabel(props.state.sweepRuntime.phase) : ''
  return `${mode} · None${progress ? ` · ${progress}` : ''}`
})
</script>
<template>
  <nav class="menu-bar" aria-label="Application menu">
    <button
      v-for="item in menus"
      :key="item"
      type="button"
      :aria-label="`${item}, not supported`"
      :title="`${item}, not supported`"
      disabled
    >{{ item }}</button>
    <span class="menu-spacer" />
    <span
      class="status-pill"
      :class="workspace.statusTone"
      :title="displayError || serviceError"
    >{{ workspace.statusLabel }}</span>
    <span title="Sweep mode · Trigger source · authoritative progress">{{ acquisitionStatus }}</span>
    <span>Revision {{ state?.stateRevision ?? '—' }}</span>
    <span class="entity-counts">{{ entityCounts }}</span>
    <time>{{ serviceError || 'Local session' }}</time>
  </nav>
</template>
