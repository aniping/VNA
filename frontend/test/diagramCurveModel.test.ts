import assert from 'node:assert/strict'
import test from 'node:test'

import { decodeStateSnapshot } from '../src/api/stateSnapshotDecoder.ts'
import { decodeTraceDisplayFrameSet } from '../src/api/traceDisplayFrameSet.ts'
import { selectDiagramCurve } from '../src/components/diagramCurveModel.ts'

const common = {
  frameId: 4,
  traceId: 31,
  measurementId: 21,
  measurementType: 'S21',
  generation: 2,
  stateRevision: 8,
  sequenceNumber: 3,
  frequenciesHz: [1e6, 2e6, 3e6],
}

function state(format: 'logMagnitude' | 'phase' | 'smith') {
  return decodeStateSnapshot({
    stateRevision: 8,
    instrument: {
      channels: [{ id: 11, sweep: { startFrequencyHz: 1e6, stopFrequencyHz: 3e6,
        points: 3, ifBandwidthHz: 1e3, powerDbm: -10 },
      sweepMode: 'continuous', triggerSource: 'none' }],
      measurements: [{ id: 21, channelId: 11, type: 'S21' }],
      windows: [{ id: 1 }],
      traces: [{ id: 31, windowId: 1, measurementId: 21, format,
        scale: format === 'logMagnitude' ? { scalePerDivision: 10, referenceValue: 0,
          referencePosition: 8, minimum: -80, maximum: 20, unit: 'dB' } : null }],
    },
  })
}

function frame(format: 'logMagnitude' | 'phase' | 'smith') {
  const variant = format === 'logMagnitude'
    ? { format, valueUnit: 'dB', values: [-80, -30, 20] }
    : format === 'phase'
      ? { format, valueUnit: 'degree', values: [-180, 0, 179.5] }
      : { format, valueUnit: 'U', values: [[0, 0], [1, 0], [1.2, 0.2]] }
  return decodeTraceDisplayFrameSet({
    generation: 2, sequenceNumber: 3, frames: [{ ...common, ...variant }],
  }).frames[0]
}

test('selects authoritative dB samples and Scale range without deriving display values', () => {
  const snapshot = state('logMagnitude')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('logMagnitude'),
  )
  assert.deepEqual(curve, {
    kind: 'cartesian', traceId: 31, label: 'Log Magnitude', unit: 'dB',
    samples: { frequenciesHz: common.frequenciesHz, values: [-80, -30, 20] },
    range: { minimum: -80, maximum: 20 },
  })
})

test('uses the frozen Phase degree viewport and preserves backend samples', () => {
  const snapshot = state('phase')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('phase'),
  )
  assert.deepEqual(curve, {
    kind: 'cartesian', traceId: 31, label: 'Phase', unit: 'degree',
    samples: { frequenciesHz: common.frequenciesHz, values: [-180, 0, 179.5] },
    range: { minimum: -225, maximum: 225 },
  })
})

test('maps Smith pairs to the frozen complex-plane seam without unit-circle filtering', () => {
  const snapshot = state('smith')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('smith'),
  )
  assert.deepEqual(curve, {
    kind: 'smith', traceId: 31,
    samples: [{ real: 0, imaginary: 0 }, { real: 1, imaginary: 0 },
      { real: 1.2, imaginary: 0.2 }],
  })
})

test('rejects a frame whose full Trace and Measurement identity is stale', () => {
  const snapshot = state('phase')
  const stale = { ...frame('phase'), measurementType: 'S11' as const }
  assert.equal(selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], stale,
  ), null)
})
