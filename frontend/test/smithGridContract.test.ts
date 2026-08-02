import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const source = readFileSync(
  new URL('../../../../src/components/SmithGrid.vue', import.meta.url), 'utf8',
)
const template = source.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? ''
const styles = source.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1] ?? ''

test('SmithGrid renders the complete normalized-impedance construction', () => {
  assert.match(template, /viewBox="-1 -1 2 2"/)
  assert.match(template, /preserveAspectRatio="xMidYMid meet"/)
  assert.match(template, /class="grid-line grid-major outer-circle"/)
  assert.match(template, /class="grid-line grid-major center-axis"/)
  assert.match(template, /v-for="circle in resistanceCircles"/)
  assert.match(template, /v-for="circle in reactanceCircles"/)
  assert.match(template, /v-for="label in resistanceLabels"/)
  assert.match(template, /v-for="label in reactanceLabels"/)
})

test('SmithGrid clips only construction arcs and keeps every stroke device-sized', () => {
  assert.match(template, /<clipPath[\s\S]*?<circle[^>]*r="1"/)
  assert.match(template, /:clip-path="`url\(#\$\{clipId\}\)`"/)
  assert.match(styles, /\.grid-line\s*\{[^}]*vector-effect:\s*non-scaling-stroke/)
  assert.match(styles, /\.smith-grid\s*\{[^}]*width:\s*100%[^}]*height:\s*100%/)
  assert.doesNotMatch(styles, /\.smith-grid\s*\{[^}]*vector-effect/)
})
