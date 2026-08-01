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
  disconnect(code = 1006, reason = ''): void {
    this.closed = true
    this.handlers.onClose({ code, reason })
  }
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
  sockets[0].handlers.onClose({ code: 1006, reason: '' })
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

test('reports an intentionally unavailable display stream without reconnecting', async () => {
  const states: string[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(
    async () => undefined,
    { onFrame() {}, onError() {}, onConnectionChange: (state) => states.push(state) },
    environment,
  )
  await settle()
  sockets[0].connect()
  sockets[0].disconnect(1008, 'display trace unavailable')

  assert.deepEqual(states, ['connecting', 'online', 'unavailable'])
  assert.equal(reconnects.length, 0)
  stop()
})

test('does not treat another policy violation as an unavailable Trace format', async () => {
  const states: string[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(
    async () => undefined,
    { onFrame() {}, onError() {}, onConnectionChange: (state) => states.push(state) },
    environment,
  )
  await settle()
  sockets[0].connect()
  sockets[0].disconnect(1008, 'display stream capacity exceeded')

  assert.deepEqual(states, ['connecting', 'online', 'offline'])
  assert.equal(reconnects.length, 1)
  stop()
})

test('a fresh session after an unavailable format accepts a new sequence baseline', async () => {
  const received: number[] = []
  const { environment, sockets } = createEnvironment([])
  const receive = handlers((value) => { received.push(value.sequenceNumber) })
  const stopUnsupported = createLiveDisplaySession(async () => undefined, receive, environment)
  await settle()
  sockets[0].message(frame(9))
  sockets[0].disconnect(1008, 'display trace unavailable')
  stopUnsupported()

  const stopLogMagnitude = createLiveDisplaySession(async () => undefined, receive, environment)
  await settle()
  sockets[1].message(frame(1))

  assert.deepEqual(received, [9, 1])
  stopLogMagnitude()
})

test('a late unsupported close from a stopped session cannot change its replacement', async () => {
  const states: string[] = []
  const { environment, sockets } = createEnvironment([])
  const report = {
    onFrame() {}, onError() {}, onConnectionChange: (state: string) => states.push(state),
  }
  const stopOld = createLiveDisplaySession(async () => undefined, report, environment)
  await settle()
  sockets[0].connect()
  stopOld()

  const stopReplacement = createLiveDisplaySession(async () => undefined, report, environment)
  await settle()
  sockets[1].connect()
  sockets[0].handlers.onClose({ code: 1008, reason: 'display trace unavailable' })

  assert.deepEqual(states, ['connecting', 'online', 'connecting', 'online'])
  stopReplacement()
})
