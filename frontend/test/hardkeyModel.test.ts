import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import test from 'node:test'

import { hardkeyGroups } from '../src/components/hardkeyModel.ts'
import {
  parseSweepCount, parseSweepPoints, sweepProgressLabel,
} from '../src/components/sweepSofttoolModel.ts'

test('ZNB control groups retain their evidenced columns and button order', () => {
  assert.deepEqual(hardkeyGroups.map(({ title, columns, keys }) => ({
    title, columns, labels: keys.map(({ label }) => label),
  })), [
    { title: 'Trace', columns: 3,
      labels: ['Meas', 'Format', 'Scale', 'Trace Config', 'Line', 'Marker'] },
    { title: 'Stimulus', columns: 2, labels: ['Start', 'Stop', 'Center', 'Span'] },
    { title: 'Channel', columns: 3,
      labels: ['Power / Bw / Avg', 'Sweep', 'Cal', 'Channel Config', 'Trigger', 'Offset / Embed'] },
    { title: 'System', columns: 3,
      labels: ['File / Print', 'Setup', 'Tools', 'Display', 'Help', 'Preset'] },
  ])
})

test('Help exposes a question-mark visual without replacing its accessible name', () => {
  const help = hardkeyGroups.flatMap(({ keys }) => keys).find(({ label }) => label === 'Help')
  assert.equal(help?.visual, 'question')
})

test('unsupported hard keys stay unavailable in the product UI', () => {
  const unavailable = hardkeyGroups.flatMap(({ keys }) => keys)
    .filter(({ enabled }) => enabled !== true)
    .map(({ label }) => label)
  assert.deepEqual(unavailable, [
    'Trace Config', 'Line', 'Marker', 'Cal', 'Channel Config',
    'Offset / Embed', 'File / Print', 'Setup', 'Tools', 'Display', 'Help', 'Preset',
  ])
})

test('Sweep and Restart entries route to semantic controls with honest disabled boundaries', () => {
  const source = readFileSync(new URL('../../../../src/components/SweepSofttool.vue', import.meta.url), 'utf8')
    + readFileSync(new URL('../../../../src/components/InstrumentToolbar.vue', import.meta.url), 'utf8')
  assert.equal(source.match(/<form @submit\.prevent=/g)?.length, 1)
  assert.match(source, /Start Sweep[\s\S]*Restart Sweep/)
  assert.match(source, /name="trigger-source"[\s\S]*disabled/)
  assert.doesNotMatch(source, /Apply Sweeps|Configured|Applied/)
})

test('Sweep drafts and status labels use frozen authority ranges and progress', () => {
  assert.deepEqual(['7', '0', '100001'].map(parseSweepCount), [7, null, null])
  assert.deepEqual(['201', '1'].map(parseSweepPoints), [201, null])
  const runtime = { generation: 2, channelId: 1, stateRevision: 3, sweepId: 8,
    userPhase: 'sweeping' as const, progress: { completedAcquisitionPoints: 51,
      totalAcquisitionPoints: 201 }, firstSweepAfterConfiguration: true,
    activePreviewIdentity: { generation: 2, sweepId: 8 } }
  assert.equal(sweepProgressLabel(runtime), 'Sweeping 51/201 (25%) *')
})
