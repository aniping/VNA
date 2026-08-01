import assert from 'node:assert/strict'
import test from 'node:test'

import {
  replaceLatestDisplayFrame,
  retainDisplayableFrames,
} from '../src/api/displayFrameState.ts'
import type { TraceDisplayFrame } from '../src/api/traceDisplayFrame.ts'

function frame(traceId: number, sequenceNumber: number): TraceDisplayFrame {
  return {
    frameId: traceId * 10 + sequenceNumber,
    traceId,
    stateRevision: 0,
    sequenceNumber,
    format: 'logMagnitude',
    valueUnit: 'dB',
    frequenciesHz: [1e6, 2e6],
    values: [-70, -68],
  }
}

test('replaces one Trace frame without mutating the prior latest-frame Map', () => {
  const first = frame(1, 1)
  const other = frame(2, 3)
  const previous = new Map([[1, first], [2, other]])
  const latest = frame(1, 2)

  const next = replaceLatestDisplayFrame(previous, latest)

  assert.equal(previous.get(1), first)
  assert.equal(next.get(1), latest)
  assert.equal(next.get(2), other)
  assert.notEqual(next, previous)
})

test('retains last-good frames only for current LogMagnitude Traces', () => {
  const traces = [
    { id: 1, format: 'logMagnitude' },
    { id: 2, format: 'phase' },
  ] as const
  const frames = new Map([[1, frame(1, 2)], [2, frame(2, 3)], [3, frame(3, 1)]])

  assert.deepEqual([...retainDisplayableFrames(frames, traces).keys()], [1])
})
