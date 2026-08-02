import assert from 'node:assert/strict'
import test from 'node:test'

import {
  noMeasurementDataMessage,
  selectDisplayDiagrams,
} from '../src/components/diagramModel.ts'
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

function createAllSParameterSnapshot(): StateSnapshot {
  const state = structuredClone(snapshot)
  state.instrument.measurements = [
    { id: 21, channelId: 11, type: 'S21' },
    { id: 22, channelId: 11, type: 'S22' },
    { id: 23, channelId: 11, type: 'S11' },
    { id: 24, channelId: 11, type: 'S12' },
  ]
  state.instrument.windows = [{ id: 31 }, { id: 32 }, { id: 33 }, { id: 34 }]
  const presetTrace = state.instrument.traces[0]
  state.instrument.traces = [
    presetTrace,
    { ...presetTrace, id: 42, windowId: 32, measurementId: 22 },
    { ...presetTrace, id: 43, windowId: 33, measurementId: 23 },
    { ...presetTrace, id: 44, windowId: 34, measurementId: 24 },
  ]
  return state
}

test('one real Window produces exactly one Diagram with its related S21 Trace', () => {
  const diagrams = selectDisplayDiagrams(snapshot)

  assert.equal(diagrams.length, 1)
  assert.deepEqual(diagrams[0], {
    windowId: 31,
    active: true,
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

test('active Diagram follows its selected Trace without changing Window order', () => {
  const state = structuredClone(snapshot)
  state.instrument.windows.push({ id: 32 })
  state.instrument.traces.push({ ...state.instrument.traces[0], id: 42, windowId: 32 })

  const activeByWindow = (traceId: number) => selectDisplayDiagrams(state, traceId)
    .map(({ windowId, active }) => ({ windowId, active }))

  assert.deepEqual({
    first: activeByWindow(41),
    second: activeByWindow(42),
  }, {
    first: [{ windowId: 31, active: true }, { windowId: 32, active: false }],
    second: [{ windowId: 31, active: false }, { windowId: 32, active: true }],
  })
})

test('a complete two-port set uses the finite ZNB S-Parameter matrix', () => {
  const diagrams = selectDisplayDiagrams(createAllSParameterSnapshot(), 41)

  assert.deepEqual(
    diagrams.map(({ measurement, trace, active }) => ({
      measurement: measurement?.type,
      traceId: trace?.id,
      active,
    })),
    [
      { measurement: 'S11', traceId: 43, active: false },
      { measurement: 'S12', traceId: 44, active: false },
      { measurement: 'S21', traceId: 41, active: true },
      { measurement: 'S22', traceId: 42, active: false },
    ],
  )
})

test('an incomplete four-Window state preserves authoritative Window order', () => {
  const state = createAllSParameterSnapshot()
  state.instrument.traces = state.instrument.traces.filter((trace) => trace.measurementId !== 23)

  assert.deepEqual(
    selectDisplayDiagrams(state, 41).map(({ windowId }) => windowId),
    [31, 32, 33, 34],
  )
})

test('four S-Parameters across Channels do not activate the finite matrix', () => {
  const state = createAllSParameterSnapshot()
  state.instrument.channels.push({ ...state.instrument.channels[0], id: 12 })
  const s11 = state.instrument.measurements.find((item) => item.type === 'S11')
  if (s11) s11.channelId = 12

  assert.deepEqual(
    selectDisplayDiagrams(state, 41).map(({ windowId }) => windowId),
    [31, 32, 33, 34],
  )
})

test('a Window with multiple Traces does not activate the finite matrix', () => {
  const state = createAllSParameterSnapshot()
  state.instrument.traces.push({ ...state.instrument.traces[0], id: 45 })

  assert.deepEqual(
    selectDisplayDiagrams(state, 41).map(({ windowId }) => windowId),
    [31, 32, 33, 34],
  )
})

test('every supported format keeps the same grid empty state while awaiting its next frame set', () => {
  assert.equal(noMeasurementDataMessage, 'No measurement data')
})
