import assert from 'node:assert/strict'
import test from 'node:test'

import { decodeTraceDisplayFrameSet } from '../src/api/traceDisplayFrameSet.ts'

const common = {
  frameId: 41,
  measurementId: 21,
  measurementType: 'S21',
  generation: 3,
  stateRevision: 7,
  sequenceNumber: 9,
  frequenciesHz: [1e6, 2e6, 3e6],
}

const logMagnitudeFrame = {
  ...common,
  traceId: 11,
  format: 'logMagnitude',
  valueUnit: 'dB',
  values: [-71, -69, -73],
}

test('decodes a valid LogMagnitude frame set', () => {
  const payload = { generation: 3, sequenceNumber: 9, frames: [logMagnitudeFrame] }
  assert.deepEqual(decodeTraceDisplayFrameSet(payload), payload)
})

test('decodes all three display payloads and all four S-parameters', () => {
  const frames = [
    { ...logMagnitudeFrame, traceId: 11, measurementType: 'S11' },
    {
      ...common, traceId: 12, measurementType: 'S21', format: 'phase',
      valueUnit: 'degree', values: [-45, 0, 45],
    },
    {
      ...common, traceId: 13, measurementType: 'S12', format: 'smith',
      valueUnit: 'U', values: [[-1, 0], [0, 1], [1, 0]],
    },
    { ...logMagnitudeFrame, traceId: 14, measurementType: 'S22' },
  ]
  const payload = { generation: 3, sequenceNumber: 9, frames }
  assert.deepEqual(decodeTraceDisplayFrameSet(payload), payload)
})

test('rejects an entire set when samples or shared set identity are inconsistent', () => {
  const second = { ...logMagnitudeFrame, traceId: 12 }
  const payload = { generation: 3, sequenceNumber: 9, frames: [logMagnitudeFrame, second] }
  const oversized = Array.from({ length: 2049 }, (_, index) => index + 1)
  const invalidPayloads = [
    { ...payload, generation: 0 },
    { ...payload, sequenceNumber: 0 },
    { ...payload, frames: [] },
    { ...payload, frames: [{ ...logMagnitudeFrame, generation: 4 }, second] },
    { ...payload, frames: [{ ...logMagnitudeFrame, sequenceNumber: 8 }, second] },
    { ...payload, frames: [{ ...logMagnitudeFrame, frameId: 0 }] },
    { ...payload, frames: [logMagnitudeFrame, { ...second, frameId: 42 }] },
    { ...payload, frames: [logMagnitudeFrame, { ...second, stateRevision: 8 }] },
    { ...payload, frames: [logMagnitudeFrame, { ...second, frequenciesHz: [1e6, 2e6, 4e6] }] },
    { ...payload, frames: [logMagnitudeFrame, { ...second, traceId: 11 }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, values: [-70, Number.NaN, -72] }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, values: [-70, -72] }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, frequenciesHz: [1e6], values: [-70] }] },
    { ...payload, frames: [{ ...logMagnitudeFrame,
      frequenciesHz: oversized, values: oversized }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, frequenciesHz: [1e6, 1e6, 3e6] }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, measurementType: 'S99' }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, format: 'polar' }] },
    { ...payload, frames: [{ ...logMagnitudeFrame, valueUnit: 'degree' }] },
    { ...payload, frames: [{ ...common, traceId: 11, format: 'smith', valueUnit: 'U',
      values: [[0, 0], [1, Number.NaN], [0, 1]] }] },
  ]
  for (const value of invalidPayloads) {
    assert.throws(() => decodeTraceDisplayFrameSet(value), /Invalid display frame set/)
  }
})
