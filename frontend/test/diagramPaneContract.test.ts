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

test('DiagramPane delegates all three backend display formats to projection components', () => {
  assert.match(paneSource, /selectDiagramCurve/)
  assert.match(paneSource, /<CartesianCurve[\s\S]*?curve\?\.kind === 'cartesian'/)
  assert.match(paneSource, /:unit="curve\.unit"/)
  assert.match(paneSource, /<SmithCurve[\s\S]*?curve\?\.kind === 'smith'/)
  assert.match(paneSource, /phaseAxisRange\.maximum/)
  assert.match(paneSource, /phaseAxisRange\.minimum/)
  assert.doesNotMatch(paneSource, /log10|atan2|impedance|admittance/i)
})

test('Cartesian and Smith grids remain mounted independently of measurement frames', () => {
  assert.match(openingTag('plot-area'), /:class="kind"/)
  assert.match(styles, /\.plot-area\.cartesian\s*\{[^}]*background-image:/)
  const smithGrid = openingTag('smith-grid')
  assert.match(smithGrid, /v-if="kind === 'smith'"/)
  assert.doesNotMatch(smithGrid, /curve|frame/)
})

test('Smith grid strokes stay device-sized instead of covering the plot', () => {
  assert.match(styles, /\.smith-grid\s+:is\(circle, line\)\s*\{[^}]*vector-effect:\s*non-scaling-stroke/)
  const svgStyle = styles.match(/\.smith-grid\s*\{([^}]*)\}/)?.[1] ?? ''
  assert.doesNotMatch(svgStyle, /vector-effect/)
})
