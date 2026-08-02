import assert from 'node:assert/strict'
import test from 'node:test'

import { segmentedSvgPath } from '../src/plot/segmentedSvgPath.ts'

test('starts every projected sample segment with a new SVG move command', () => {
  assert.equal(segmentedSvgPath([
    [{ x: 0, y: 1 }, { x: 0.2, y: 0.5 }],
    [{ x: 0.8, y: 0.5 }, { x: 1, y: 0 }],
  ]), 'M 0 1 L 0.2 0.5 M 0.8 0.5 L 1 0')
})

test('keeps a complete single segment path unchanged', () => {
  assert.equal(segmentedSvgPath([
    [{ x: 0, y: 1 }, { x: 0.5, y: 0.5 }, { x: 1, y: 0 }],
  ]), 'M 0 1 L 0.5 0.5 L 1 0')
})
