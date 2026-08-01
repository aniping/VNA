import assert from 'node:assert/strict'
import test from 'node:test'

import { projectCartesianPoints } from '../src/plot/cartesianProjection.ts'

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

test('projects Phase endpoints and leaves out-of-domain values for viewport clipping', () => {
  const points = projectCartesianPoints({
    frequenciesHz: [1, 2, 3, 4],
    values: [-180, 0, 180, 240],
  }, { minimum: -180, maximum: 180 })

  assert.deepEqual(points, [
    { x: 0, y: 1 },
    { x: 1 / 3, y: 0.5 },
    { x: 2 / 3, y: 0 },
    { x: 1, y: -1 / 6 },
  ])
})
