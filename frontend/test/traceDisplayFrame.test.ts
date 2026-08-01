import assert from 'node:assert/strict'
import test from 'node:test'

import { decodeTraceDisplayFrame } from '../src/api/traceDisplayFrame.ts'

const validFrame = {
  frameId: 31,
  traceId: 7,
  stateRevision: 0,
  sequenceNumber: 12,
  format: 'logMagnitude',
  valueUnit: 'dB',
  frequenciesHz: [1e6, 2e6, 3e6],
  values: [-71, -69, -73],
}

test('decodes the frozen text-message display frame payload', () => {
  assert.deepEqual(decodeTraceDisplayFrame(validFrame), validFrame)
})

test('rejects identities, formats, units, and sample arrays outside the frozen contract', () => {
  const invalidFrames = [
    { ...validFrame, frameId: 0 },
    { ...validFrame, traceId: 1.5 },
    { ...validFrame, stateRevision: -1 },
    { ...validFrame, sequenceNumber: 0 },
    { ...validFrame, format: 'phase' },
    { ...validFrame, valueUnit: 'degree' },
    { ...validFrame, frequenciesHz: [1e6], values: [-70] },
    { ...validFrame, frequenciesHz: [1e6, 1e6, 3e6] },
    { ...validFrame, values: [-70, Number.NaN, -71] },
    { ...validFrame, values: [-70, -71] },
    { ...validFrame, frequenciesHz: Array.from({ length: 2049 }, (_, index) => index + 1),
      values: Array.from({ length: 2049 }, () => -70) },
  ]

  for (const value of invalidFrames) {
    assert.throws(() => decodeTraceDisplayFrame(value), /Invalid display frame response/)
  }
})
