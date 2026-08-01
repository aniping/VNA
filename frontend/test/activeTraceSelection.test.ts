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
