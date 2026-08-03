import assert from 'node:assert/strict'
import test from 'node:test'
import { decodeSweepPreviewEvent } from '../src/api/sweepPreview.ts'
function status(overrides: Record<string, unknown> = {}) {
  return {
    generation: 4,
    channelId: 1,
    stateRevision: 9,
    sweepId: 12,
    userPhase: 'sweeping',
    progress: { completedAcquisitionPoints: 2, totalAcquisitionPoints: 6 },
    firstSweepAfterConfiguration: true,
    activePreviewIdentity: { generation: 4, sweepId: 12 },
    ...overrides,
  }
}
function trace(format: 'logMagnitude' | 'phase' | 'smith') {
  const variant = format === 'logMagnitude'
    ? { format, valueUnit: 'dB', values: [-70, -65] }
    : format === 'phase'
      ? { format, valueUnit: 'degree', values: [-45, 10] }
      : { format, valueUnit: 'U', values: [[0, 0], [0.2, -0.1]] }
  return {
    traceId: 7,
    measurementId: 3,
    measurementType: 'S21',
    frequenciesHz: [1e6, 2e6],
    ...variant,
  }
}
function available(overrides: Record<string, unknown> = {}) {
  return {
    type: 'available',
    eventCursor: 8,
    generation: 4,
    sweepId: 12,
    channelId: 1,
    stateRevision: 9,
    sequenceNumber: 2,
    totalPointCount: 6,
    traces: [trace('logMagnitude')],
    sweepStatus: status(),
    ...overrides,
  }
}
test('decodes cumulative dB, Phase, and Smith preview samples without RF conversion', () => {
  for (const format of ['logMagnitude', 'phase', 'smith'] as const) {
    const event = decodeSweepPreviewEvent(available({ traces: [trace(format)] }))
    assert.equal(event.type, 'available')
    assert.equal(event.traces[0].format, format)
    assert.deepEqual(event.traces[0].frequenciesHz, [1e6, 2e6])
  }
  const onePoint = { ...trace('phase'), frequenciesHz: [1e6], values: [-30] }
  const event = decodeSweepPreviewEvent(available({ traces: [onePoint] }))
  if (event.type !== 'available') assert.fail('expected available preview')
  assert.equal(event.traces[0].values.length, 1)
  assert.equal(event.sweepStatus.firstSweepAfterConfiguration, true)
})
test('rejects malformed sample, progress, and identity atoms as whole events', () => {
  const invalid = [
    available({ traces: [{ ...trace('phase'), values: [Number.NaN, 0] }] }),
    available({ sweepStatus: status({ generation: 5 }) }),
    { type: 'status', eventCursor: 3, sweepStatus: status({ userPhase: 'publishing' }) },
  ]
  for (const value of invalid) {
    assert.throws(() => decodeSweepPreviewEvent(value), /Invalid/)
  }
})
