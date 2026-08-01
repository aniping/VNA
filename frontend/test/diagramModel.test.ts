import assert from 'node:assert/strict'
import test from 'node:test'

import { selectDisplayDiagrams } from '../src/components/diagramModel.ts'
import type { StateSnapshot } from '../src/api/vnaApi.ts'

const snapshot: StateSnapshot = {
  stateRevision: 7,
  instrument: {
    channels: [{
      id: 11,
      sweep: {
        startFrequencyHz: 1e6,
        stopFrequencyHz: 1e9,
        points: 201,
        ifBandwidthHz: 1e3,
        powerDbm: -10,
      },
      sweepMode: 'continuous',
      triggerSource: 'none',
    }],
    measurements: [{ id: 21, channelId: 11, type: 'S21' }],
    windows: [{ id: 31 }],
    traces: [{
      id: 41,
      windowId: 31,
      measurementId: 21,
      format: 'logMagnitude',
      scale: {
        scalePerDivision: 10,
        referenceValue: 0,
        referencePosition: 8,
        minimum: -80,
        maximum: 20,
        unit: 'dB',
      },
    }],
  },
}

test('one real Window produces exactly one Diagram with its related S21 Trace', () => {
  const diagrams = selectDisplayDiagrams(snapshot, 41)

  assert.equal(diagrams.length, 1)
  assert.deepEqual(diagrams[0], {
    windowId: 31,
    trace: snapshot.instrument.traces[0],
    measurement: snapshot.instrument.measurements[0],
    channel: snapshot.instrument.channels[0],
  })
})

test('multiple real Windows remain ordered without synthesizing placeholder Diagrams', () => {
  const state = structuredClone(snapshot)
  state.instrument.windows.push({ id: 32 })

  assert.deepEqual(
    selectDisplayDiagrams(state, 41).map(({ windowId, trace }) => ({
      windowId,
      traceId: trace?.id,
    })),
    [
      { windowId: 31, traceId: 41 },
      { windowId: 32, traceId: undefined },
    ],
  )
})
