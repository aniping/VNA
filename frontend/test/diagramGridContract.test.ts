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
