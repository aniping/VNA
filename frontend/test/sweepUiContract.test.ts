import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

import {
  statusBarChannelLabel, statusBarSweepLabel, sweepBoundaryKey,
  sweepControlUnavailableItems, sweepSofttoolPages, triggerSourceItems,
} from '../src/components/sweepSofttoolModel.ts'

const source = (path: string): string => readFileSync(
  new URL(`../../../../${path}`, import.meta.url), 'utf8',
)

test('Sweep pages and unavailable controls follow the ZNB vertical order', () => {
  assert.deepEqual(sweepSofttoolPages.map(({ id }) => id), ['control', 'trigger', 'parameters'])
  assert.deepEqual(triggerSourceItems, ['Free Run', 'External', 'Multiple', 'Manual'])
  assert.deepEqual(sweepControlUnavailableItems, [
    'Restart Manager', 'All Channels Continuous', 'All Channels on Hold',
    'Sweep Controller', 'Pipelining',
  ])
})

test('status model exposes only authoritative channel and sweep information', () => {
  assert.equal(statusBarChannelLabel({ id: 1 }), 'Ch1: Avg None')
  assert.equal(statusBarSweepLabel(null, 'preparing'), 'Preparing')
  assert.equal(statusBarSweepLabel(null, undefined), 'Sweep —')
  const status = { generation: 2, channelId: 1, stateRevision: 4, sweepId: 8,
    userPhase: 'sweeping' as const, progress: { completedAcquisitionPoints: 2,
      totalAcquisitionPoints: 4 }, firstSweepAfterConfiguration: true,
    activePreviewIdentity: { generation: 2, sweepId: 8 } }
  assert.equal(statusBarSweepLabel(status, 'hold'), 'Sweeping 2/4 (50%) *')
  assert.equal(sweepBoundaryKey({ ...status, userPhase: 'hold' }), '2:8:4')
  assert.equal(sweepBoundaryKey(status), null)
})

test('status bar removes development diagnostics and Sweep uses honest controls', () => {
  const status = source('src/components/InstrumentStatusBar.vue')
  const sweep = source('src/components/SweepSofttool.vue')
  const model = source('src/components/sweepSofttoolModel.ts')
  assert.doesNotMatch(status, /ONLINE|Revision|entityCounts|Local session|Continuous · None/)
  assert.match(status, /statusBarChannelLabel[\s\S]*statusBarSweepLabel/)
  assert.match(sweep, /<strong>Sweep<\/strong>[\s\S]*aria-label="Close Sweep"/)
  assert.match(sweep, /class="sweep-pages"/)
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
