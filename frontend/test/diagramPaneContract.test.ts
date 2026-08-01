import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

// Tests execute from the compiled cache; this URL deliberately reaches the source SFC contract.
const paneSource = readFileSync(
  new URL('../../../../src/components/DiagramPane.vue', import.meta.url),
  'utf8',
)
const template = paneSource.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? ''
const styles = paneSource.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1] ?? ''

function openingTag(className: string): string {
  return template.match(new RegExp(`<[^>]+class="${className}"[^>]*>`))?.[0] ?? ''
}

test('active Diagram styles identifiers without changing the pane frame', () => {
  const pane = openingTag('diagram-pane')
  const paneFrame = styles.match(/\.diagram-pane\s*\{([^}]*)\}/)?.[1] ?? ''

  assert.match(pane, /:aria-current="active \? 'true' : undefined"/)
  assert.doesNotMatch(pane, /:class=/)
  assert.match(openingTag('trace-strip'), /:class="\{ active \}"/)
  assert.match(openingTag('diagram-identifier'), /:class="\{ active \}"/)
  assert.match(openingTag('channel-id'), /:class="\{ active \}"/)
  assert.match(paneFrame, /border:\s*1px solid #60717a/)
  assert.doesNotMatch(styles, /\.diagram-pane\.active|#f2db24|box-shadow/)
})
