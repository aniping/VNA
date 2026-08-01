import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const panelSource = readFileSync(
  new URL('../../../../src/components/HardkeyPanel.vue', import.meta.url), 'utf8',
)
const workbenchStyles = readFileSync(
  new URL('../../../../src/styles.css', import.meta.url), 'utf8',
)
const template = panelSource.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? ''
const styles = panelSource.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1] ?? ''

test('HardkeyPanel renders evidenced columns and a non-text Help visual', () => {
  assert.match(template, /--hardkey-columns.*group\.columns/)
  assert.match(template, /key\.visual === 'question'/)
  assert.match(template, /class="help-symbol"[^>]*aria-hidden="true"/)
  assert.match(template, /:aria-label="key\.visual \? key\.label : undefined"/)
  assert.match(template, /:disabled="!key\.enabled/)
  assert.match(styles, /repeat\(var\(--hardkey-columns\), minmax\(0, 1fr\)\)/)
  assert.match(styles, /\.hardkey-panel[^}]*padding:\s*4px 5px 5px/)
})

test('1280 by 800 workbench keeps toolbar, dock, and bottom bar inside the canvas', () => {
  assert.match(workbenchStyles, /\.instrument-canvas[^}]*width:\s*1280px[^}]*height:\s*800px/)
  assert.match(workbenchStyles, /\.main-screen[^}]*grid-template-rows:\s*1fr 34px/)
  assert.match(workbenchStyles, /\.application-workspace\s*\{[^}]*1fr 160px/)
  assert.match(workbenchStyles, /\.application-workspace\.softtool-visible[^}]*848px 272px 160px/)
  assert.match(workbenchStyles, /\.measurement-stage[^}]*grid-template-rows:\s*42px 1fr/)
})
