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
  constructor(readonly handlers: DisplayFrameSocketHandlers) {}
  close(): void { this.closed = true }
  message(value: object): void { this.handlers.onMessage(JSON.stringify(value)) }
  connect(): void { this.handlers.onOpen() }
  disconnect(): void {
    this.closed = true
    this.handlers.onClose({ code: 1006, reason: '' })
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

function handlers(
  onFrameSet: LiveDisplayHandlers['onFrameSet'] = () => undefined,
): LiveDisplayHandlers {
  return { onFrameSet, onError() {}, onConnectionChange() {} }
}

function frameSet(generation: number, sequenceNumber: number): object {
  return {
    generation,
    sequenceNumber,
    frames: [{
      frameId: generation * 100 + sequenceNumber,
      traceId: 7,
      measurementId: 2,
      measurementType: 'S21',
      generation,
      stateRevision: 4,
      sequenceNumber,
      format: 'logMagnitude',
      valueUnit: 'dB',
      frequenciesHz: [1e6, 2e6],
      values: [-70, -68],
    }],
  }
}

test('refreshes state before opening the first frame-set connection', async () => {
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

test('accepts only forward generation and sequence pairs on one connection', async () => {
  const received: Array<[number, number]> = []
  const { environment, sockets } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, handlers((value) => {
    received.push([value.generation, value.sequenceNumber])
  }), environment)
  await settle()
  sockets[0].message(frameSet(3, 9))
  sockets[0].message(frameSet(3, 9))
  sockets[0].message(frameSet(3, 8))
  sockets[0].message(frameSet(4, 1))
  sockets[0].message(frameSet(3, 10))
  sockets[0].message(frameSet(4, 2))
  assert.deepEqual(received, [[3, 9], [4, 1], [4, 2]])
  stop()
})

test('refreshes before reconnect and gives the new connection a fresh baseline', async () => {
  const events: string[] = []
  const received: Array<[number, number]> = []
  const { environment, sockets, reconnects } = createEnvironment(events)
  const stop = createLiveDisplaySession(
    async () => { events.push('refresh') },
    handlers((value) => { received.push([value.generation, value.sequenceNumber]) }),
    environment,
  )
  await settle()
  sockets[0].message(frameSet(9, 20))
  sockets[0].disconnect()
  sockets[0].message(frameSet(10, 1))
  reconnects[0]()
  await settle()
  sockets[1].message(frameSet(1, 1))
  sockets[0].message(frameSet(10, 2))
  assert.deepEqual(events, ['refresh', 'connect', 'refresh', 'connect'])
  assert.deepEqual(received, [[9, 20], [1, 1]])
  stop()
})

test('stop cancels reconnect and ignores late socket events', async () => {
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
  sockets[0].message(frameSet(1, 1))
  await settle()
  assert.equal(sockets[0].closed, true)
  assert.equal(sockets.length, 1)
  assert.equal(refreshes, 1)
  assert.deepEqual(received, [])
})

test('reports transport state and reconnects every actual socket close', async () => {
  const states: string[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(
    async () => undefined,
    { onFrameSet() {}, onError() {}, onConnectionChange: (state) => states.push(state) },
    environment,
  )
  await settle()
  sockets[0].connect()
  sockets[0].handlers.onClose({ code: 1008, reason: 'display trace unavailable' })
  assert.equal(reconnects.length, 1)
  reconnects[0]()
  await settle()
  sockets[1].connect()
  assert.deepEqual(states, ['connecting', 'online', 'offline', 'connecting', 'online'])
  stop()
})

test('invalid sets report a display error without closing the healthy connection', async () => {
  const errors: string[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, {
    onFrameSet() {},
    onError: (error) => errors.push(error.message),
    onConnectionChange() {},
  }, environment)
  await settle()
  sockets[0].message({ generation: 1, sequenceNumber: 1, frames: [] })
  assert.match(errors[0], /frames must not be empty/)
  assert.equal(reconnects.length, 0)
  stop()
})
