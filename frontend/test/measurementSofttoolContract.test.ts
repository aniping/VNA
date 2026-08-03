import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

const readSource = (path: string): string => readFileSync(
  new URL(`../../../../${path}`, import.meta.url), 'utf8',
)
const softtool = readSource('src/components/MeasurementSofttool.vue')
const mainScreen = readSource('src/components/MainScreen.vue')
const app = readSource('src/App.vue')
const template = softtool.match(/<template>([\s\S]*?)<\/template>/)?.[1] ?? ''
const styles = softtool.match(/<style scoped>([\s\S]*?)<\/style>/)?.[1] ?? ''

test('Meas exposes the four choices and one All S-Params command', () => {
  assert.doesNotMatch(softtool, /Format|Create Trace|New Channel|TraceSetup|ChannelSetup/)
  assert.doesNotMatch(softtool, /@keydown|@keyup|@submit/)
  assert.match(softtool, /All S-Params/)
  assert.match(softtool, /S-Param Wizard/)
  assert.match(softtool, /Balanced Ports/)
  const buttons = [...template.matchAll(/<button\b[\s\S]*?>/g)].map(([button]) => button)
  const choices = buttons.filter((button) => button.includes('v-for="parameter in sParameters"'))
  assert.equal(choices.length, 1)
  assert.match(choices[0], /:disabled="isMeasurementChoiceDisabled\(/)
  assert.match(choices[0], /@click="emit\('updateMeasurementType', parameter\)"/)
  const allButton = buttons.find((button) => button.includes('isAllSParametersDisabled')) ?? ''
  assert.match(allButton, /:disabled="isAllSParametersDisabled\(hasActiveTrace, disabled, busy\)"/)
  assert.match(allButton, /@click="emit\('ensureAllSParameters'\)"/)
  assert.match(softtool, /hasActiveTrace: boolean/)
  assert.match(softtool, /ensureAllSParameters: \[\]/)
  const unsupported = buttons.filter((button) => !choices.includes(button) && button !== allButton)
  assert.equal(unsupported.every((button) => /\sdisabled(?:\s|>)/.test(button)), true)
  assert.equal(unsupported.every((button) => !button.includes('@click')), true)
  assert.match(template, /measurementType === parameter/)
  assert.match(styles, /\.measurement-softtool[^}]*overflow:\s*hidden/)
  assert.match(styles, /\.softtool-body[^}]*grid-template-columns:\s*165px 1fr/)
})

test('production UI has no create Channel, Measurement, Trace, or Window path', () => {
  const productionSources = `${app}\n${mainScreen}`
  const runner = app.match(
    /async function runCommand[\s\S]*?\r?\n}\r?\n\r?\nasync function handleUpdateSweep/,
  )?.[0] ?? ''
  const handler = app.match(
    /async function handleUpdateTraceMeasurementType[\s\S]*?\r?\n}\r?\n\r?\nasync function handleEnsure/,
  )?.[0] ?? ''
  assert.doesNotMatch(productionSources, /createChannel|createMeasurement|createTrace|createWindow/)
  assert.match(mainScreen, /<MeasurementSofttool[\s\S]*?:measurement-type=[\s\S]*?:busy=/)
  assert.match(mainScreen, /@update-measurement-type="forwardMeasurementTypeUpdate"/)
  assert.match(mainScreen, /emit\('updateTraceMeasurementType', activeTrace\.value\.id/)
  assert.match(runner, /if \(!snapshot \|\| commandBusy\.value\) return/)
  assert.match(handler, /runCommand\(async \(snapshot\) =>/)
  assert.match(handler, /await setTraceMeasurementType\([\s\S]*?pendingFrameTraceId = traceId[\s\S]*?removeLiveDisplayTrace\([\s\S]*?await refreshState\(\)/)
  assert.equal(handler.match(/setTraceMeasurementType\(/g)?.length, 1)
  assert.equal(handler.match(/refreshState\(\)/g)?.length, 1)
  assert.doesNotMatch(handler, /state\.value\s*=/)
  assert.match(app, /acceptCompleteFrameSet\(display\.value, frameSet, state\.value\)/)
  assert.match(app, /onFrameSet: replaceFrameSet/)
})
