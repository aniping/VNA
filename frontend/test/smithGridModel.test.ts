import assert from 'node:assert/strict'
import test from 'node:test'

import {
  formatNormalizedLabel,
  normalizedMagnitudeRadii,
  normalizedReactanceMagnitudes,
  normalizedResistanceCircleValues,
  normalizedResistanceLabelValues,
  reactanceCircle,
  reactanceLabelPoint,
  resistanceCircle,
  resistanceLabelPoint,
} from '../src/components/smithGridModel.ts'

test('constructs the ZNB normalized-resistance circles in the Gamma plane', () => {
  assert.deepEqual(normalizedResistanceCircleValues, [0.2, 0.5, 1, 2, 5])
  assert.deepEqual(normalizedResistanceLabelValues, [0, 0.2, 0.5, 1, 2, 5, 10, Infinity])
  assert.deepEqual(normalizedMagnitudeRadii, [0.2, 0.4, 0.6, 0.8])
  assert.deepEqual(resistanceCircle(1), { centerX: 0.5, centerY: 0, radius: 0.5 })
  assert.deepEqual(resistanceCircle(5), {
    centerX: 5 / 6, centerY: 0, radius: 1 / 6,
  })
  assert.deepEqual(resistanceLabelPoint(0), { x: -1, y: 0 })
  assert.deepEqual(resistanceLabelPoint(2), { x: 1 / 3, y: 0 })
  assert.deepEqual(resistanceLabelPoint(Infinity), { x: 1, y: 0 })
})

test('constructs positive and negative normalized-reactance arcs for SVG coordinates', () => {
  assert.deepEqual(normalizedReactanceMagnitudes, [0.2, 0.5, 1, 2, 5])
  assert.deepEqual(reactanceCircle(1), { centerX: 1, centerY: -1, radius: 1 })
  assert.deepEqual(reactanceCircle(-0.5), { centerX: 1, centerY: 2, radius: 2 })
  assert.deepEqual(reactanceLabelPoint(1), { x: 0, y: -1 })
  assert.deepEqual(reactanceLabelPoint(-2), { x: 0.6, y: 0.8 })
})

test('formats normalized labels with the compact ZNB decimal tokens', () => {
  assert.equal(formatNormalizedLabel(0.2), '.2')
  assert.equal(formatNormalizedLabel(-0.5), '-.5')
  assert.equal(formatNormalizedLabel(Infinity), '∞')
})
