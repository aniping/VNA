import assert from 'node:assert/strict'
import test from 'node:test'

import { selectWorkspacePresentation } from '../src/components/workspacePresentation.ts'

const emptyState = { instrument: { windows: [], traces: [] } }
const diagramState = { instrument: { windows: [{ id: 1 }], traces: [{ id: 1 }] } }

test('initial display error is visible and never reports Online', () => {
  const view = selectWorkspacePresentation({
    state: null,
    connection: 'online',
    hasFrame: false,
    displayError: 'Invalid state response',
  })

  assert.equal(view.mode, 'fault')
  assert.equal(view.statusLabel, 'OFFLINE')
  assert.equal(view.headline, 'Service unavailable')
  assert.equal(view.showDiagrams, false)
})

test('first live frame error keeps a valid configured Diagram visible', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'online',
    hasFrame: false,
    displayError: 'Invalid display frame',
  })

  assert.equal(view.mode, 'fault')
  assert.equal(view.statusLabel, 'OFFLINE')
  assert.equal(view.headline, 'Service unavailable')
  assert.equal(view.showDiagrams, true)
})

test('valid state without Window or Trace has an explained Online empty state', () => {
  const view = selectWorkspacePresentation({
    state: emptyState,
    connection: 'online',
    hasFrame: false,
    displayError: '',
  })

  assert.equal(view.mode, 'empty')
  assert.equal(view.statusLabel, 'ONLINE')
  assert.equal(view.headline, 'No available Diagram')
  assert.equal(view.showDiagrams, false)
})

test('empty display configuration stays explicit while live display reconnects', () => {
  const view = selectWorkspacePresentation({
    state: emptyState,
    connection: 'offline',
    hasFrame: false,
    displayError: '',
  })

  assert.equal(view.mode, 'empty')
  assert.equal(view.statusLabel, 'RECONNECTING')
  assert.equal(view.headline, 'No available Diagram')
  assert.equal(view.showDiagrams, false)
})

test('valid display configuration is visible Online before its first frame', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'online',
    hasFrame: false,
    displayError: '',
  })

  assert.equal(view.statusLabel, 'ONLINE')
  assert.equal(view.showDiagrams, true)
})

test('valid display configuration stays visible while reconnecting before its first frame', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'offline',
    hasFrame: false,
    displayError: '',
  })

  assert.equal(view.statusLabel, 'RECONNECTING')
  assert.equal(view.showDiagrams, true)
})

test('valid display configuration reports Connecting while its socket opens', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'connecting',
    hasFrame: false,
    displayError: '',
  })

  assert.equal(view.statusLabel, 'CONNECTING')
  assert.equal(view.showDiagrams, true)
})

test('an online display error with empty state does not claim a reconnect', () => {
  const view = selectWorkspacePresentation({
    state: emptyState,
    connection: 'online',
    hasFrame: false,
    displayError: 'Invalid display frame',
  })

  assert.equal(view.mode, 'empty')
  assert.equal(view.statusLabel, 'OFFLINE')
  assert.equal(view.headline, 'No available Diagram')
  assert.equal(view.showDiagrams, false)
})

test('reconnecting with last-good state and frame keeps diagrams visibly stale', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'offline',
    hasFrame: true,
    displayError: '',
  })

  assert.equal(view.mode, 'stale')
  assert.equal(view.statusLabel, 'RECONNECTING · STALE')
  assert.equal(view.showDiagrams, true)
})

test('a bad live frame preserves last-good data without claiming a reconnect', () => {
  const view = selectWorkspacePresentation({
    state: diagramState,
    connection: 'online',
    hasFrame: true,
    displayError: 'Invalid display frame',
  })

  assert.equal(view.mode, 'stale')
  assert.equal(view.statusLabel, 'DISPLAY ERROR · STALE')
  assert.equal(view.showDiagrams, true)
})
