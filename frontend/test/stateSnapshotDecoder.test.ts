import assert from 'node:assert/strict'
import test from 'node:test'

import { decodeStateSnapshot } from '../src/api/stateSnapshotDecoder.ts'
import { fetchState } from '../src/api/vnaApi.ts'

const validSnapshot = {
  stateRevision: 0,
  instrument: {
    channels: [{
      id: 1,
      sweep: {
        startFrequencyHz: 1e6, stopFrequencyHz: 1e9, points: 201,
        ifBandwidthHz: 1e3, powerDbm: -10,
      },
      sweepMode: 'continuous',
      sweepCount: 1,
      triggerSource: 'none',
    }],
    measurements: [{ id: 1, channelId: 1, type: 'S21' }],
    windows: [{ id: 1 }],
    traces: [{
      id: 1,
      windowId: 1,
      measurementId: 1,
      format: 'logMagnitude',
      scale: {
        scalePerDivision: 10, referenceValue: 0, referencePosition: 8,
        minimum: -80, maximum: 20,
        unit: 'dB',
      },
    }],
  },
  sweepRuntime: {
    state: 'running', phase: 'sweeping',
    configured: { stateRevision: 0, mode: 'continuous', sweepCount: 1 },
    applied: { stateRevision: 0, generation: 1, mode: 'continuous', sweepCount: 1 },
  },
}

test('decodes authoritative Continuous sweep and None trigger state', () => {
  const snapshot = decodeStateSnapshot(validSnapshot)
  assert.equal(snapshot.instrument.channels[0].sweepMode, 'continuous')
  assert.equal(snapshot.instrument.channels[0].sweepCount, 1)
  assert.equal(snapshot.instrument.channels[0].triggerSource, 'none')
  assert.equal(snapshot.sweepRuntime.applied.generation, 1)
  const single = decodeStateSnapshot({
    ...validSnapshot,
    instrument: { ...validSnapshot.instrument, channels: [{
      ...validSnapshot.instrument.channels[0], sweepMode: 'single', sweepCount: 7,
    }] },
  })
  assert.equal(single.instrument.channels[0].sweepMode, 'single')
})

test('decodes all frozen two-port S-parameter measurement identities', () => {
  const measurements = ['S11', 'S21', 'S12', 'S22'].map((type, index) => ({
    id: index + 1, channelId: 1, type,
  }))
  const snapshot = decodeStateSnapshot({
    ...validSnapshot,
    instrument: { ...validSnapshot.instrument, measurements },
  })
  assert.deepEqual(snapshot.instrument.measurements.map(({ type }) => type),
    ['S11', 'S21', 'S12', 'S22'])
})

test('rejects state outside the frozen channel and display contract', () => {
  const channel = validSnapshot.instrument.channels[0]
  const trace = validSnapshot.instrument.traces[0]
  const invalidSnapshots = [
    { ...validSnapshot, stateRevision: -1 },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument, channels: 'invalid' } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      channels: [{ ...channel, sweepCount: 0 }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      channels: [{ ...channel, triggerSource: 'external' }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      channels: [{ ...channel, sweep: { ...channel.sweep, startFrequencyHz: -1.5 } }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      channels: [{ ...channel, sweep: { ...channel.sweep, ifBandwidthHz: 0.5 } }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      channels: [{ ...channel, sweep: { ...channel.sweep, points: 0x1_0000_0000 } }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      measurements: [{ id: 1, channelId: 1, type: 'S99' }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      traces: [{ ...trace, format: 'polar' }] } },
    { ...validSnapshot, instrument: { ...validSnapshot.instrument,
      traces: [{ ...trace, scale: { ...trace.scale, unit: 'degree' } }] } },
    { ...validSnapshot, sweepRuntime: { ...validSnapshot.sweepRuntime, phase: 'publishing' } },
    { ...validSnapshot, sweepRuntime: { ...validSnapshot.sweepRuntime,
      applied: { ...validSnapshot.sweepRuntime.applied, generation: 0 } } },
  ]
  for (const value of invalidSnapshots) {
    assert.throws(() => decodeStateSnapshot(value), /Invalid state response/)
  }
})

test('fetchState exposes only a decoded authoritative snapshot', async () => {
  const originalFetch = globalThis.fetch
  globalThis.fetch = async () => new Response(JSON.stringify({
    ...validSnapshot,
    sweepRuntime: { ...validSnapshot.sweepRuntime, phase: 'publishing' },
  }))
  try {
    await assert.rejects(fetchState(), /Invalid state response/)
  } finally {
    globalThis.fetch = originalFetch
  }
})
