import assert from 'node:assert/strict'
import test from 'node:test'
import {
  acceptCompleteFrameSet,
  acceptSweepPreviewEvent,
  emptyLiveDisplayState,
  retainLiveDisplayForSnapshot,
} from '../src/api/displayFrameSetState.ts'
import { decodeStateSnapshot } from '../src/api/stateSnapshotDecoder.ts'
import { decodeSweepPreviewEvent } from '../src/api/sweepPreview.ts'
import { decodeTraceDisplayFrameSet } from '../src/api/traceDisplayFrameSet.ts'
const snapshot = decodeStateSnapshot({
  stateRevision: 9,
  instrument: {
    channels: [{ id: 1, sweep: { startFrequencyHz: 1e6, stopFrequencyHz: 4e6,
      points: 4, ifBandwidthHz: 1e3, powerDbm: -10 },
    sweepMode: 'continuous', sweepCount: 1, triggerSource: 'none' }],
    measurements: [{ id: 3, channelId: 1, type: 'S21' }],
    windows: [{ id: 1 }],
    traces: [{ id: 7, windowId: 1, measurementId: 3, format: 'logMagnitude',
      scale: { scalePerDivision: 10, referenceValue: 0, referencePosition: 9,
        minimum: -90, maximum: 10, unit: 'dB' } }],
  },
  sweepRuntime: {
    state: 'running', phase: 'sweeping',
    configured: { stateRevision: 9, mode: 'continuous', sweepCount: 1 },
    applied: { stateRevision: 9, generation: 4, mode: 'continuous', sweepCount: 1 },
  },
})
function status(overrides: Record<string, unknown> = {}) {
  return {
    generation: 4, channelId: 1, stateRevision: 9, sweepId: 12,
    userPhase: 'sweeping',
    progress: { completedAcquisitionPoints: 2, totalAcquisitionPoints: 4 },
    firstSweepAfterConfiguration: false,
    activePreviewIdentity: { generation: 4, sweepId: 12 },
    ...overrides,
  }
}
function preview(overrides: Record<string, unknown> = {}) {
  return decodeSweepPreviewEvent({
    type: 'available', eventCursor: 8, generation: 4, sweepId: 12,
    channelId: 1, stateRevision: 9, sequenceNumber: 2, totalPointCount: 4,
    traces: [{ traceId: 7, measurementId: 3, measurementType: 'S21',
      format: 'logMagnitude', valueUnit: 'dB',
      frequenciesHz: [1e6, 2e6], values: [-70, -65] }],
    sweepStatus: status(),
    ...overrides,
  })
}
function complete(generation = 4) {
  return decodeTraceDisplayFrameSet({
    generation, sequenceNumber: 1,
    frames: [{ frameId: 5, traceId: 7, measurementId: 3, measurementType: 'S21',
      generation, stateRevision: 9, sequenceNumber: 1,
      format: 'logMagnitude', valueUnit: 'dB', frequenciesHz: [1e6, 2e6, 3e6, 4e6],
      values: [-80, -75, -70, -65] }],
  })
}
test('keeps last complete and current cumulative partial in one compatible generation', () => {
  const withComplete = acceptCompleteFrameSet(emptyLiveDisplayState(), complete(), snapshot)
  const state = acceptSweepPreviewEvent(withComplete, preview(), snapshot)
  assert.deepEqual([...state.lastComplete.keys()], [7])
  assert.deepEqual(state.currentPartial!.axis, {
    frequencyMinimumHz: 1e6,
    frequencyMaximumHz: 4e6,
  })
})
test('generation advance atomically clears old complete and partial lanes', () => {
  const populated = acceptSweepPreviewEvent(
    acceptCompleteFrameSet(emptyLiveDisplayState(), complete(), snapshot), preview(), snapshot,
  )
  assert.equal(acceptCompleteFrameSet(populated, complete(5), snapshot).sweepStatus, null)
  const advanced = decodeSweepPreviewEvent({
    type: 'generationAdvanced', eventCursor: 9, generation: 5,
    sweepStatus: status({ generation: 5, sweepId: null, activePreviewIdentity: null }),
  })
  const state = acceptSweepPreviewEvent(populated, advanced, snapshot)
  assert.equal(state.generation, 5)
  assert.equal(state.currentPartial, null)
  assert.equal(acceptSweepPreviewEvent(state, preview(), snapshot), state)
})
test('invalidation clears only the exact active Sweep partial', () => {
  const current = acceptSweepPreviewEvent(emptyLiveDisplayState(), preview(), snapshot)
  const other = decodeSweepPreviewEvent({
    type: 'invalidated', eventCursor: 9, generation: 4, sweepId: 13,
    sweepStatus: status({ sweepId: null, activePreviewIdentity: null }),
  })
  const exact = decodeSweepPreviewEvent({
    type: 'invalidated', eventCursor: 10, generation: 4, sweepId: 12,
    sweepStatus: status({ sweepId: null, activePreviewIdentity: null }),
  })
  assert.notEqual(acceptSweepPreviewEvent(current, other, snapshot).currentPartial, null)
  assert.equal(acceptSweepPreviewEvent(current, exact, snapshot).currentPartial, null)
})
test('rejects incompatible axes and carries the proven axis across configured-only changes', () => {
  const current = acceptSweepPreviewEvent(emptyLiveDisplayState(), preview(), snapshot)
  const wrongRevision = preview({
    stateRevision: 10,
    sweepStatus: status({ stateRevision: 10 }),
  })
  assert.equal(acceptSweepPreviewEvent(current, wrongRevision, snapshot), current)
  assert.equal(acceptSweepPreviewEvent(current, preview({ totalPointCount: 5 }), snapshot), current)
  const reconfigured = decodeStateSnapshot({
    ...snapshot, stateRevision: 10,
    sweepRuntime: { ...snapshot.sweepRuntime,
      configured: { stateRevision: 10, mode: 'continuous', sweepCount: 1 } },
  })
  const continued = acceptSweepPreviewEvent(current, preview({ sequenceNumber: 3 }), reconfigured)
  assert.deepEqual(continued.currentPartial?.axis, current.currentPartial?.axis)
})
test('authoritative refresh resets a stale process generation before reconnect baselines', () => {
  const current = { ...acceptCompleteFrameSet(emptyLiveDisplayState(), complete(), snapshot),
    generation: 99 }
  const retained = retainLiveDisplayForSnapshot(current, snapshot)
  assert.equal(retained.generation, 4)
  assert.equal(retained.lastComplete.size, 0)
})
