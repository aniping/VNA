import assert from 'node:assert/strict'
import test from 'node:test'

import { decodeStateSnapshot } from '../src/api/stateSnapshotDecoder.ts'
import { decodeTraceDisplayFrameSet } from '../src/api/traceDisplayFrameSet.ts'
import { selectDiagramCurve } from '../src/components/diagramCurveModel.ts'
import type { CurrentSweepPartial } from '../src/api/displayFrameSetState.ts'
import type { TraceDisplaySamples } from '../src/api/traceDisplayFrameSet.ts'

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
      sweepMode: 'continuous', sweepCount: 1, triggerSource: 'none' }],
      measurements: [{ id: 21, channelId: 11, type: 'S21' }],
      windows: [{ id: 1 }],
      traces: [{ id: 31, windowId: 1, measurementId: 21, format,
        scale: format === 'logMagnitude' ? { scalePerDivision: 10, referenceValue: 0,
          referencePosition: 8, minimum: -80, maximum: 20, unit: 'dB' } : null }],
    },
    sweepRuntime: {
      state: 'running', phase: 'sweeping',
      configured: { stateRevision: 8, mode: 'continuous', sweepCount: 1 },
      applied: { stateRevision: 8, generation: 2, mode: 'continuous', sweepCount: 1 },
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

function partial(format: 'logMagnitude' | 'phase' | 'smith'): CurrentSweepPartial {
  const variant = format === 'logMagnitude'
    ? { format, valueUnit: 'dB', values: [-60, -55] }
    : format === 'phase'
      ? { format, valueUnit: 'degree', values: [-90, 30] }
      : { format, valueUnit: 'U', values: [[0.1, 0.2], [0.2, 0.3]] }
  const preview = {
    traceId: 31, measurementId: 21, measurementType: 'S21',
    frequenciesHz: [1e6, 2e6], ...variant,
  } as TraceDisplaySamples
  return {
    generation: 2, sweepId: 9, stateRevision: 8, totalPointCount: 3,
    traces: new Map([[31, preview]]),
    axis: { frequencyMinimumHz: 1e6, frequencyMaximumHz: 3e6 },
  }
}

test('selects authoritative dB samples and Scale range without deriving display values', () => {
  const snapshot = state('logMagnitude')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('logMagnitude'),
  )
  assert.deepEqual(curve, {
    kind: 'cartesian', traceId: 31, label: 'Log Magnitude', unit: 'dB',
    samples: {
      frequencyMinimumHz: 1e6,
      frequencyMaximumHz: 3e6,
      segments: [{ frequenciesHz: common.frequenciesHz, values: [-80, -30, 20] }],
    },
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
    samples: {
      frequencyMinimumHz: 1e6,
      frequencyMaximumHz: 3e6,
      segments: [{ frequenciesHz: common.frequenciesHz, values: [-180, 0, 179.5] }],
    },
    range: { minimum: -225, maximum: 225 },
  })
})

test('wraps a complete Cartesian frame as exactly one explicit sample segment', () => {
  const snapshot = state('phase')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('phase'),
  )
  assert.equal(curve?.kind === 'cartesian' ? curve.samples.segments.length : 0, 1)
})

test('wraps a complete Smith frame as exactly one explicit sample segment', () => {
  const snapshot = state('smith')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('smith'),
  )
  assert.equal(curve?.kind === 'smith' ? curve.segments.length : 0, 1)
})

test('maps Smith pairs to the frozen complex-plane seam without unit-circle filtering', () => {
  const snapshot = state('smith')
  const curve = selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], frame('smith'),
  )
  assert.deepEqual(curve, {
    kind: 'smith', traceId: 31,
    segments: [[{ real: 0, imaginary: 0 }, { real: 1, imaginary: 0 },
      { real: 1.2, imaginary: 0.2 }]],
  })
})

test('rejects a frame whose full Trace and Measurement identity is stale', () => {
  const snapshot = state('phase')
  const stale = { ...frame('phase'), measurementType: 'S11' as const }
  assert.equal(selectDiagramCurve(
    snapshot.instrument.traces[0], snapshot.instrument.measurements[0], stale,
  ), null)
})

test('replaces the acquired Cartesian prefix and retains only the old suffix', () => {
  const snapshot = state('logMagnitude')
  const curve = selectDiagramCurve(snapshot.instrument.traces[0],
    snapshot.instrument.measurements[0], frame('logMagnitude'), partial('logMagnitude'))
  assert.equal(curve?.kind, 'cartesian')
  if (curve?.kind !== 'cartesian') return
  assert.equal(curve.samples.segments.length, 2)
  assert.deepEqual(curve.samples.segments[0].values, [-60, -55])
  assert.deepEqual(curve.samples.segments[1].values, [20])
  assert.deepEqual([curve.samples.frequencyMinimumHz, curve.samples.frequencyMaximumHz],
    [1e6, 3e6])
})

test('replaces the acquired Smith prefix and retains only the old suffix', () => {
  const snapshot = state('smith')
  const curve = selectDiagramCurve(snapshot.instrument.traces[0],
    snapshot.instrument.measurements[0], frame('smith'), partial('smith'))
  assert.equal(curve?.kind, 'smith')
  if (curve?.kind !== 'smith') return
  assert.equal(curve.segments.length, 2)
  assert.deepEqual(curve.segments[0], [
    { real: 0.1, imaginary: 0.2 }, { real: 0.2, imaginary: 0.3 },
  ])
  assert.deepEqual(curve.segments[1], [{ real: 1.2, imaginary: 0.2 }])
})
