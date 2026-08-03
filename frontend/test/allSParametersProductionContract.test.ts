import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const readSource = (path: string): string => readFileSync(
  new URL(`../../../../${path}`, import.meta.url), 'utf8',
)
const app = readSource('src/App.vue')
const mainScreen = readSource('src/components/MainScreen.vue')

test('MainScreen forwards All S-Params only for its active Trace', () => {
  assert.match(mainScreen, /ensureAllSParameters: \[traceId: number\]/)
  assert.match(mainScreen, /:has-active-trace="Boolean\(activeTrace\)"/)
  assert.match(mainScreen, /@ensure-all-s-parameters="forwardAllSParameters"/)
  assert.match(mainScreen, /if \(activeTrace\.value\) emit\('ensureAllSParameters', activeTrace\.value\.id\)/)
})

test('App clears old frames only after one successful ensure command, then refreshes', () => {
  const handler = app.match(
    /async function handleEnsureAllSParameters[\s\S]*?\r?\n}\r?\n\r?\nfunction replaceFrameSet/,
  )?.[0] ?? ''
  const command = handler.indexOf('await ensureAllSParameters(')
  const clear = handler.indexOf('display.value = clearLiveDisplayData(display.value)')
  const refresh = handler.indexOf('await refreshState()')

  assert.match(handler, /runCommand\(async \(snapshot\) =>/)
  assert.match(handler, /const previousRevision = snapshot\.stateRevision/)
  assert.match(handler, /pendingAllSParametersRevision = result\.stateRevision[\s\S]*?clearLiveDisplayData/)
  assert.equal(handler.match(/ensureAllSParameters\(/g)?.length, 1)
  assert.equal(handler.match(/refreshState\(\)/g)?.length, 1)
  assert.equal(command >= 0 && command < clear && clear < refresh, true)
  assert.doesNotMatch(handler, /state\.value\s*=/)
  assert.doesNotMatch(`${app}\n${mainScreen}`, /createMeasurement|createTrace|createWindow/)
})

test('App blocks late frame sets until refreshed state receives one complete publication', () => {
  assert.match(app, /let pendingAllSParametersRevision: number \| null = null/)
  assert.match(app, /state\.value\.stateRevision < pendingAllSParametersRevision/)
  assert.match(app, /replaceCompleteDisplayFramesForSnapshot\([\s\S]*?minimumRevision/)
  assert.match(app, /pendingAllSParametersRevision = null/)
})
