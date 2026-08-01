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

test('Meas is display-only until a trace measurement command exists', () => {
  assert.doesNotMatch(softtool, /Format|Create Trace|New Channel|TraceSetup|ChannelSetup/)
  assert.doesNotMatch(softtool, /defineEmits|@click|@submit/)
  assert.match(softtool, /All S-Params/)
  assert.match(softtool, /S-Param Wizard/)
  assert.match(softtool, /Balanced Ports/)
  const buttons = [...template.matchAll(/<button\b[\s\S]*?>/g)].map(([button]) => button)
  assert.equal(buttons.length > 0, true)
  assert.equal(buttons.every((button) => /\bdisabled\b/.test(button)), true)
  assert.match(template, /measurementType === parameter/)
  assert.match(styles, /\.measurement-softtool[^}]*overflow:\s*hidden/)
  assert.match(styles, /\.softtool-body[^}]*grid-template-columns:\s*165px 1fr/)
})

test('production UI has no create Channel, Measurement, Trace, or Window path', () => {
  const productionSources = `${app}\n${mainScreen}`
  assert.doesNotMatch(productionSources, /createChannel|createMeasurement|createTrace|createWindow/)
  assert.match(mainScreen, /<MeasurementSofttool[\s\S]*?:measurement-type=/)
})
