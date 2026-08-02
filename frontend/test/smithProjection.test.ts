import assert from 'node:assert/strict'
import test from 'node:test'

import {
  projectSmithPoints,
  projectSmithSegments,
} from '../src/plot/smithProjection.ts'

test('projects the frozen complex plane axes into SVG coordinates', () => {
  const points = projectSmithPoints([
    { real: 0, imaginary: 0 },
    { real: -1, imaginary: 0 },
    { real: 1, imaginary: 0 },
    { real: 0, imaginary: 1 },
    { real: 0, imaginary: -1 },
  ])

  assert.deepEqual(points, [
    { x: 0, y: 0 },
    { x: -1, y: 0 },
    { x: 1, y: 0 },
    { x: 0, y: -1 },
    { x: 0, y: 1 },
  ])
})

test('preserves a legal point outside the unit circle without domain clipping', () => {
  assert.deepEqual(
    projectSmithPoints([{ real: 0.8, imaginary: 0.8 }]),
    [{ x: 0.8, y: -0.8 }],
  )
})

test('projects an empty Smith sample set as an empty path input', () => {
  assert.deepEqual(projectSmithPoints([]), [])
})

test('preserves explicit Smith segment boundaries while projecting the complex plane', () => {
  assert.deepEqual(projectSmithSegments([
    [{ real: -1, imaginary: 0 }, { real: 0, imaginary: 1 }],
    [{ real: 0.5, imaginary: -0.5 }, { real: 1, imaginary: 0 }],
  ]), [
    [{ x: -1, y: 0 }, { x: 0, y: -1 }],
    [{ x: 0.5, y: 0.5 }, { x: 1, y: 0 }],
  ])
})
