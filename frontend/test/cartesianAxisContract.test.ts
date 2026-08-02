import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const source = readFileSync(
  new URL('../../../../src/components/CartesianAxisOverlay.vue', import.meta.url), 'utf8',
)
const template = source.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? ''
const styles = source.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1] ?? ''

test('Cartesian axis renders eleven authoritative major tick labels', () => {
  assert.match(source, /cartesianAxisTicks\(props\.axis\)/)
  assert.match(template, /v-for="\(tick, index\) in ticks"/)
  assert.match(template, /formatTick\(tick, axis\.unit\)/)
  assert.match(styles, /\.axis-tick\s*\{[^}]*position:\s*absolute/)
})

test('reference value uses a same-Trace dashed line and right-edge triangle', () => {
  assert.match(template, /class="reference-line"/)
  assert.match(template, /:style="\{ top: referenceTop \}"/)
  assert.match(styles, /\.reference-line\s*\{[^}]*dashed[^}]*var\(--trace-color/)
  assert.match(styles, /\.reference-marker\s*\{[^}]*border-right[^}]*var\(--trace-color/)
})
