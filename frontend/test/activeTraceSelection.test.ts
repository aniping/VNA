import assert from 'node:assert/strict'
import test from 'node:test'
import { nextTick, ref } from 'vue'

import type { StateSnapshot } from '../src/api/vnaApi.ts'
import { useActiveTrace } from '../src/components/useActiveTrace.ts'

function snapshot(type: 'S21' | 'S11', measurementId: number): StateSnapshot {
  return {
    stateRevision: measurementId,
    instrument: {
      channels: [],
      measurements: [{ id: measurementId, channelId: 1, type }],
      windows: [{ id: 7 }],
      traces: [{
        id: 3, windowId: 7, measurementId, format: 'logMagnitude', scale: null,
      }],
    },
  }
}

test('authoritative measurement refresh preserves the active Trace and Window', async () => {
  const state = ref<StateSnapshot | null>(snapshot('S21', 11))
  const selection = useActiveTrace(state)
  assert.equal(selection.activeTrace.value?.id, 3)
  assert.equal(selection.activeTrace.value?.windowId, 7)

  state.value = snapshot('S11', 12)
  await nextTick()

  assert.equal(selection.activeTraceId.value, 3)
  assert.equal(selection.activeTrace.value?.windowId, 7)
  assert.equal(selection.activeMeasurement.value?.type, 'S11')
})

test('adding the All S-Parameter quartet keeps the preset S21 Trace active', async () => {
  const initial = snapshot('S21', 11)
  const state = ref<StateSnapshot | null>(initial)
  const selection = useActiveTrace(state)
  const expanded = structuredClone(initial)
  expanded.instrument.windows.push({ id: 8 }, { id: 9 }, { id: 10 })
  expanded.instrument.traces.push(
    { ...expanded.instrument.traces[0], id: 4, windowId: 8 },
    { ...expanded.instrument.traces[0], id: 5, windowId: 9 },
    { ...expanded.instrument.traces[0], id: 6, windowId: 10 },
  )

  state.value = expanded
  await nextTick()

  assert.equal(selection.activeTraceId.value, 3)
  assert.equal(selection.activeTrace.value?.windowId, 7)
})
