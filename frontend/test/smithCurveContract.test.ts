import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const curveSource = readFileSync(
  new URL('../../../../src/components/SmithCurve.vue', import.meta.url),
  'utf8',
)

test('SmithCurve exposes complex samples on an equal-aspect SVG viewport', () => {
  assert.match(curveSource, /samples: readonly SmithComplexPoint\[\]/)
  assert.match(curveSource, /viewBox="-1 -1 2 2"/)
  assert.match(curveSource, /preserveAspectRatio="xMidYMid meet"/)
  assert.match(curveSource, /v-if="pathData"/)
  assert.doesNotMatch(curveSource, /clipPath|clip-path|mask/)
})
