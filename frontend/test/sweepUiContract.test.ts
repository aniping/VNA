import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

import {
  nextSweepPage, statusBarChannelLabel, statusBarProgress, sweepBoundaryKey,
  sweepControlUnavailableItems, sweepSofttoolPages, triggerSourceItems,
} from '../src/components/sweepSofttoolModel.ts'

const source = (path: string): string => readFileSync(
  new URL(`../../../../${path}`, import.meta.url), 'utf8',
)

test('Sweep pages and unavailable controls follow the ZNB vertical order', () => {
  assert.deepEqual(sweepSofttoolPages.map(({ id, page }) => ({ id, page })), [
    { id: 'parameters', page: 'parameters' },
    { id: 'type', page: 'type' },
    { id: 'trigger', page: 'trigger' },
    { id: 'trigger-out', page: 'trigger-out' },
    { id: 'control', page: 'control' },
  ])
  assert.deepEqual(triggerSourceItems, ['Free Run', 'External', 'Multiple', 'Manual'])
  assert.deepEqual(sweepSofttoolPages.map(({ page }) => nextSweepPage(page)), [
    'type', 'trigger', 'trigger-out', 'control', 'parameters',
  ])
  assert.deepEqual(sweepControlUnavailableItems, [
    'Restart Manager', 'All Channels Continuous', 'All Channels on Hold',
    'Sweep Controller', 'Pipelining',
  ])
})

test('status model exposes only authoritative channel and sweep information', () => {
  assert.equal(statusBarChannelLabel({ id: 1 }), 'Ch1: Avg None')
  assert.deepEqual(statusBarProgress(null, 'preparing'), {
    label: 'Preparing', percent: 0, firstSweep: false,
  })
  assert.deepEqual(statusBarProgress(null, undefined), {
    label: 'Sweep —', percent: 0, firstSweep: false,
  })
  const status = { generation: 2, channelId: 1, stateRevision: 4, sweepId: 8,
    userPhase: 'sweeping' as const, progress: { completedAcquisitionPoints: 2,
      totalAcquisitionPoints: 4 }, firstSweepAfterConfiguration: true,
    activePreviewIdentity: { generation: 2, sweepId: 8 } }
  assert.deepEqual(statusBarProgress(status, 'hold'), {
    label: 'Ch1 50%', percent: 50, firstSweep: true,
  })
  assert.equal(sweepBoundaryKey({ ...status, userPhase: 'hold' }), '2:8:4')
  assert.equal(sweepBoundaryKey(status), null)
})

test('status bar removes development diagnostics and Sweep uses honest controls', () => {
  const status = source('src/components/InstrumentStatusBar.vue')
  const sweep = source('src/components/SweepSofttool.vue')
  const model = source('src/components/sweepSofttoolModel.ts')
  const sweepContract = sweep + model
  assert.doesNotMatch(status, /ONLINE|Revision|entityCounts|Local session|Continuous · None/)
  assert.match(status, /statusBarChannelLabel[\s\S]*statusBarProgress/)
  assert.match(status, /role="progressbar"[\s\S]*status-progress-fill/)
  assert.match(sweep, /<strong>Sweep<\/strong>[\s\S]*aria-label="Close Sweep"/)
  assert.match(sweep, /v-for="item in sweepSofttoolPages"/)
  assert.match(sweep, /@click="emit\('selectPage', item\.page\)"/)
  assert.doesNotMatch(sweep, /<h2>|margin-left: -8px/)
  for (const label of [
    'Number of Points', 'Freq Step Size', 'Sweep Time', 'Meas Delay',
    'All Partial', 'First Partial', 'Freq Sweep Mode',
    'Lin Freq', 'Log Freq', 'Segmented', 'CW Mode', 'Define Segments', 'Seg X-Axis',
    'Manual Trigger', 'Sequence', 'Delay', 'Signal Type',
    'Trigger Out Active', 'Interval', 'Polarity', 'Duration', 'Position',
  ]) assert.match(sweepContract, new RegExp(label))
  assert.match(sweep, /Start Sweep/)
  assert.match(sweep, /@keydown\.enter\.prevent="updateCount"/)
  assert.doesNotMatch(sweep, /Apply Sweeps|<span>×<\/span>/)
  assert.match(sweep, /class="unavailable-row"[\s\S]*disabled/)
  for (const label of sweepControlUnavailableItems) assert.match(model, new RegExp(label))
  for (const label of triggerSourceItems.slice(1)) assert.match(model, new RegExp(label))
})

test('production boundary refresh is single-shot per authoritative Hold identity', () => {
  const app = source('src/App.vue')
  assert.match(app, /sweepBoundaryKey\(event\.sweepStatus\)/)
  assert.match(app, /refreshedSweepBoundaryKey/)
  assert.match(app, /refreshState\(\)\.catch\(handleDisplayError\)/)
  assert.doesNotMatch(app, /setInterval|pollOperation|fetchTraceDisplay/)
})
