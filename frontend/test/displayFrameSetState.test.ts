import assert from 'node:assert/strict'
import test from 'node:test'

import { filterDisplayFrameSetForSnapshot } from '../src/api/displayFrameSetState.ts'
import { decodeStateSnapshot } from '../src/api/stateSnapshotDecoder.ts'
import { decodeTraceDisplayFrameSet } from '../src/api/traceDisplayFrameSet.ts'

const measurementTypes = ['S21', 'S11', 'S12', 'S22', 'S21']
const snapshot = decodeStateSnapshot({
  stateRevision: 99,
  instrument: {
    channels: [{
      id: 1,
      sweep: { startFrequencyHz: 1e6, stopFrequencyHz: 3e6, points: 3,
        ifBandwidthHz: 1e3, powerDbm: -10 },
      sweepMode: 'continuous', triggerSource: 'none',
    }],
    measurements: measurementTypes.map((type, index) => ({
      id: index + 21, channelId: 1, type,
    })),
    windows: [{ id: 1 }],
    traces: [
      { id: 11, windowId: 1, measurementId: 21, format: 'logMagnitude', scale: null },
      { id: 12, windowId: 1, measurementId: 22, format: 'phase', scale: null },
      { id: 13, windowId: 1, measurementId: 23, format: 'smith', scale: null },
      { id: 14, windowId: 1, measurementId: 24, format: 'phase', scale: null },
    ],
  },
})

const common = {
  frameId: 41, generation: 3, stateRevision: 7, sequenceNumber: 9,
  frequenciesHz: [1e6, 2e6, 3e6],
}

test('retains only frames whose complete display identity matches the current snapshot', () => {
  const frameSet = decodeTraceDisplayFrameSet({
    generation: 3,
    sequenceNumber: 9,
    frames: [
      { ...common, traceId: 11, measurementId: 21, measurementType: 'S21',
        format: 'logMagnitude', valueUnit: 'dB', values: [-70, -71, -72] },
      { ...common, traceId: 12, measurementId: 22, measurementType: 'S21',
        format: 'phase', valueUnit: 'degree', values: [-45, 0, 45] },
      { ...common, traceId: 13, measurementId: 25, measurementType: 'S21',
        format: 'smith', valueUnit: 'U', values: [[0, 0], [0.1, 0.2], [1, 0]] },
      { ...common, traceId: 14, measurementId: 24, measurementType: 'S22',
        format: 'smith', valueUnit: 'U', values: [[0, 0], [0.1, 0.2], [1, 0]] },
      { ...common, traceId: 15, measurementId: 25, measurementType: 'S21',
        format: 'logMagnitude', valueUnit: 'dB', values: [-70, -71, -72] },
    ],
  })

  const filtered = filterDisplayFrameSetForSnapshot(frameSet, snapshot)
  assert.equal(filtered.generation, 3)
  assert.equal(filtered.sequenceNumber, 9)
  assert.deepEqual(filtered.frames.map(({ traceId }) => traceId), [11])
})
