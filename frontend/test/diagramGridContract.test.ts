import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const source = readFileSync(
  new URL('../../../../src/components/DiagramGrid.vue', import.meta.url), 'utf8',
)

test('one factory Window renders one pane across the complete DiagramGrid', () => {
  assert.match(source, /single:\s*diagrams\.length === 1/)
  assert.match(source, /v-for="diagram in diagrams"/)
  assert.match(source, /\.diagram-grid\.single\s*\{[^}]*grid-template-columns:\s*minmax\(0, 1fr\)[^}]*grid-template-rows:\s*minmax\(0, 1fr\)/)
})

test('four real Windows reuse the one-pane-per-Window 2 by 2 grid', () => {
  assert.match(source, /v-for="diagram in diagrams"/)
  assert.match(source, /:trace="diagram\.trace"/)
  assert.doesNotMatch(source, /:traces=/)
  assert.match(source, /\.diagram-grid\s*\{[^}]*grid-template-columns:\s*repeat\(2,[^}]*grid-template-rows:\s*repeat\(2,/)
})
