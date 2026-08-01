import assert from 'node:assert/strict'
import test from 'node:test'

import { hardkeyGroups } from '../src/components/hardkeyModel.ts'

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
    'Trace Config', 'Line', 'Marker', 'Cal', 'Channel Config', 'Trigger',
    'Offset / Embed', 'File / Print', 'Setup', 'Tools', 'Display', 'Help', 'Preset',
  ])
})
