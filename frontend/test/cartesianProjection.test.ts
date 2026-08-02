import assert from 'node:assert/strict'
import test from 'node:test'

import {
  projectCartesianPoints,
  projectCartesianSegments,
} from '../src/plot/cartesianProjection.ts'

test('projects backend Cartesian values onto the existing normalized dB viewport', () => {
  const points = projectCartesianPoints({
    frequenciesHz: [1e6, 2e6, 3e6],
    values: [-80, -30, 20],
  }, {
    minimum: -80,
    maximum: 20,
  })

  assert.deepEqual(points, [
    { x: 0, y: 1 },
    { x: 0.5, y: 0.5 },
    { x: 1, y: 0 },
  ])
})

test('keeps out-of-range values outside the viewport for SVG clipping', () => {
  const points = projectCartesianPoints({
    frequenciesHz: [1, 2],
    values: [-100, 40],
  }, {
    minimum: -80,
    maximum: 20,
  })

  assert.deepEqual(points, [{ x: 0, y: 1.2 }, { x: 1, y: -0.2 }])
})

test('projects wrapped Phase samples inside the wider ZNB default viewport', () => {
  const points = projectCartesianPoints({
    frequenciesHz: [1, 2, 3],
    values: [-180, 0, 179.5],
  }, { minimum: -225, maximum: 225 })

  assert.deepEqual(points, [
    { x: 0, y: 0.9 },
    { x: 0.5, y: 0.5 },
    { x: 1, y: 91 / 900 },
  ])
})

test('projects ordered Cartesian segments against one full frequency axis', () => {
  const segments = projectCartesianSegments({
    frequencyMinimumHz: 1,
    frequencyMaximumHz: 11,
    segments: [
      { frequenciesHz: [1, 2], values: [-80, -30] },
      { frequenciesHz: [10, 11], values: [-30, 20] },
    ],
  }, { minimum: -80, maximum: 20 })

  assert.deepEqual(segments, [
    [{ x: 0, y: 1 }, { x: 0.1, y: 0.5 }],
    [{ x: 0.9, y: 0.5 }, { x: 1, y: 0 }],
  ])
})
