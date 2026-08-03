import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const source = (path: string) => readFileSync(
  new URL(`../../../../${path}`, import.meta.url), 'utf8',
)
const app = source('src/App.vue')
const session = source('src/api/liveDisplaySession.ts')
const curveModel = source('src/components/diagramCurveModel.ts')
const production = `${app}\n${session}\n${source('src/components/MainScreen.vue')}\n${curveModel}`

test('production display consumes complete and preview sets without diagnostic polling paths', () => {
  assert.match(session, /decodeTraceDisplayFrameSet/)
  assert.match(session, /decodeSweepPreviewEvent/)
  assert.match(session, /onFrameSet/)
  assert.match(session, /onPreviewEvent/)
  assert.match(app, /acceptCompleteFrameSet/)
  assert.doesNotMatch(production, /pollOperation|fetchTraceDisplayFrame/)
  assert.doesNotMatch(production, /decodeTraceDisplayFrame\(/)
  assert.doesNotMatch(production, /Math\.log10|Math\.atan2|impedance|admittance/i)
})

test('Format changes keep the single live session instead of reconnecting per format', () => {
  const handler = app.match(
    /async function handleUpdateTraceFormat[\s\S]*?\r?\n}\r?\n\r?\nasync function/,
  )?.[0] ?? ''
  assert.match(handler, /await updateTraceFormat[\s\S]*?await refreshState/)
  assert.doesNotMatch(handler, /openLiveDisplaySession|startLiveDisplaySession/)
  assert.equal(app.match(/startLiveDisplaySession\(/g)?.length, 1)
})
