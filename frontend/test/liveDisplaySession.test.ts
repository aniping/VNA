import assert from 'node:assert/strict'
import test from 'node:test'

import {
  createLiveDisplaySession,
  type DisplayFrameSocketHandlers,
  type LiveDisplayEnvironment,
  type LiveDisplayHandlers,
} from '../src/api/liveDisplaySession.ts'

class FakeSocket {
  closed = false
  readonly handlers: DisplayFrameSocketHandlers

  constructor(handlers: DisplayFrameSocketHandlers) { this.handlers = handlers }
  close(): void { this.closed = true }
  message(frame: object): void { this.handlers.onMessage(JSON.stringify(frame)) }
  connect(): void { this.handlers.onOpen() }
  disconnect(): void { this.closed = true; this.handlers.onClose() }
}
function createEnvironment(events: string[]) {
  const sockets: FakeSocket[] = []
  const reconnects: Array<() => void> = []
  const environment: LiveDisplayEnvironment = {
    openSocket(handlers) {
      events.push('connect')
      const socket = new FakeSocket(handlers)
      sockets.push(socket)
      return socket
    },
    scheduleReconnect(callback) {
      let canceled = false
      reconnects.push(() => { if (!canceled) callback() })
      return () => { canceled = true }
    },
  }
  return { environment, sockets, reconnects }
}

async function settle(): Promise<void> { await Promise.resolve(); await Promise.resolve() }
function handlers(onFrame: LiveDisplayHandlers['onFrame'] = () => undefined): LiveDisplayHandlers {
  return { onFrame, onError() {}, onConnectionChange() {} }
}

function frame(sequenceNumber: number, traceId = 7): object {
  return {
    frameId: sequenceNumber + 20,
    traceId,
    stateRevision: 4,
    sequenceNumber,
    format: 'logMagnitude',
    valueUnit: 'dB',
    frequenciesHz: [1e6, 2e6],
    values: [-70, -68],
  }
}

test('refreshes state before opening the first display-frame connection', async () => {
  const events: string[] = []
  const { environment, sockets } = createEnvironment(events)
  const stop = createLiveDisplaySession(
    async () => { events.push('refresh') }, handlers(), environment,
  )
  await settle()
  assert.deepEqual(events, ['refresh', 'connect'])
  assert.equal(sockets.length, 1)
  stop()
})

test('delivers only strictly increasing sequences for each Trace on one connection', async () => {
  const received: Array<{ traceId: number; sequenceNumber: number }> = []
  const { environment, sockets } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, handlers((value) => {
    received.push({ traceId: value.traceId, sequenceNumber: value.sequenceNumber })
  }), environment)
  await settle()
  sockets[0].message(frame(3))
  sockets[0].message(frame(3))
  sockets[0].message(frame(2))
  sockets[0].message(frame(1, 8))
  sockets[0].message(frame(4))
  assert.deepEqual(received, [
    { traceId: 7, sequenceNumber: 3 },
    { traceId: 8, sequenceNumber: 1 },
    { traceId: 7, sequenceNumber: 4 },
  ])
  stop()
})

test('refreshes before reconnect and gives the new connection a fresh sequence baseline', async () => {
  const events: string[] = []
  const received: number[] = []
  const { environment, sockets, reconnects } = createEnvironment(events)
  const stop = createLiveDisplaySession(
    async () => { events.push('refresh') },
    handlers((value) => { received.push(value.sequenceNumber) }),
    environment,
  )
  await settle()
  sockets[0].message(frame(9))
  sockets[0].disconnect()
  assert.equal(reconnects.length, 1)
  sockets[0].message(frame(10))
  reconnects[0]()
  await settle()
  sockets[1].message(frame(1))
  sockets[0].message(frame(11))
  assert.deepEqual(events, ['refresh', 'connect', 'refresh', 'connect'])
  assert.deepEqual(received, [9, 1])
  stop()
})

test('stop cancels a scheduled reconnect and ignores late events', async () => {
  let refreshes = 0
  const received: number[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(
    async () => { refreshes += 1 },
    handlers((value) => { received.push(value.sequenceNumber) }),
    environment,
  )
  await settle()
  sockets[0].disconnect()
  stop()
  reconnects[0]()
  sockets[0].message(frame(1))
  await settle()
  assert.equal(sockets[0].closed, true)
  assert.equal(sockets.length, 1)
  assert.equal(refreshes, 1)
  assert.deepEqual(received, [])
})

test('stop closes an active socket without scheduling a reconnect', async () => {
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, handlers(), environment)
  await settle()
  stop()
  sockets[0].handlers.onClose()
  assert.equal(sockets[0].closed, true)
  assert.equal(reconnects.length, 0)
})

test('reports connecting, online, and reconnecting transport state', async () => {
  const states: string[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(
    async () => undefined,
    { onFrame() {}, onError() {}, onConnectionChange: (state) => states.push(state) },
    environment,
  )
  await settle()
  sockets[0].connect()
  sockets[0].disconnect()
  reconnects[0]()
  await settle()
  sockets[1].connect()

  assert.deepEqual(states, ['connecting', 'online', 'offline', 'connecting', 'online'])
  stop()
})
