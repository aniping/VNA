import assert from 'node:assert/strict'
import test from 'node:test'

import {
  createLiveDisplaySession,
  type DisplayFrameSocketHandlers,
  type LiveDisplayEnvironment,
  type LiveDisplayHandlers,
  type LiveDisplayStream,
} from '../src/api/liveDisplaySession.ts'

class FakeSocket {
  closed = false
  constructor(readonly stream: LiveDisplayStream, readonly handlers: DisplayFrameSocketHandlers) {}
  close(): void { this.closed = true }
  message(value: object): void { this.handlers.onMessage(JSON.stringify(value)) }
  connect(): void { this.handlers.onOpen() }
  disconnect(): void { this.handlers.onClose({ code: 1006, reason: '' }) }
}

function createEnvironment(events: string[]) {
  const sockets: FakeSocket[] = []
  const reconnects: Array<() => void> = []
  const environment: LiveDisplayEnvironment = {
    openSocket(stream, handlers) {
      events.push(`connect:${stream}`)
      const socket = new FakeSocket(stream, handlers)
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

function handlers(overrides: Partial<LiveDisplayHandlers> = {}): LiveDisplayHandlers {
  return {
    onFrameSet() {}, onPreviewEvent() {}, onError() {}, onConnectionChange() {}, ...overrides,
  }
}

function frameSet(generation: number, sequenceNumber: number): object {
  return { generation, sequenceNumber, frames: [{
    frameId: generation * 100 + sequenceNumber,
    traceId: 7, measurementId: 2, measurementType: 'S21', generation,
    stateRevision: 4, sequenceNumber, format: 'logMagnitude', valueUnit: 'dB',
    frequenciesHz: [1e6, 2e6], values: [-70, -68],
  }] }
}

function preview(eventCursor: number): object {
  return {
    type: 'status', eventCursor,
    sweepStatus: {
      generation: 1, channelId: 1, stateRevision: 4, sweepId: null, userPhase: 'hold',
      progress: { completedAcquisitionPoints: 0, totalAcquisitionPoints: 0 },
      firstSweepAfterConfiguration: false, activePreviewIdentity: null,
    },
  }
}

async function settle(): Promise<void> { await Promise.resolve(); await Promise.resolve() }

test('refreshes state before opening one complete and one preview lane', async () => {
  const events: string[] = []
  const states: string[] = []
  const { environment, sockets } = createEnvironment(events)
  const stop = createLiveDisplaySession(async () => { events.push('refresh') }, handlers({
    onConnectionChange: (state) => states.push(state),
  }), environment)
  await settle()
  assert.deepEqual(events, ['refresh', 'connect:complete', 'connect:preview'])
  sockets[0].connect()
  assert.deepEqual(states, ['connecting'])
  sockets[1].connect()
  assert.deepEqual(states, ['connecting', 'online'])
  stop()
})

test('filters complete tuples and preview cursors independently on one connection', async () => {
  const frames: Array<[number, number]> = []
  const cursors: number[] = []
  const { environment, sockets } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, handlers({
    onFrameSet: ({ generation, sequenceNumber }) => frames.push([generation, sequenceNumber]),
    onPreviewEvent: ({ eventCursor }) => cursors.push(eventCursor),
  }), environment)
  await settle()
  sockets[0].message(frameSet(3, 9)); sockets[0].message(frameSet(3, 9))
  sockets[0].message(frameSet(4, 1)); sockets[0].message(frameSet(3, 10))
  sockets[1].message(preview(5)); sockets[1].message(preview(5)); sockets[1].message(preview(4))
  sockets[1].message(preview(6))
  assert.deepEqual(frames, [[3, 9], [4, 1]])
  assert.deepEqual(cursors, [5, 6])
  stop()
})

test('one lane closing reconnects the pair after refresh with fresh baselines', async () => {
  const events: string[] = []
  const frames: number[] = []
  const cursors: number[] = []
  const { environment, sockets, reconnects } = createEnvironment(events)
  const stop = createLiveDisplaySession(async () => { events.push('refresh') }, handlers({
    onFrameSet: ({ sequenceNumber }) => frames.push(sequenceNumber),
    onPreviewEvent: ({ eventCursor }) => cursors.push(eventCursor),
  }), environment)
  await settle()
  sockets[0].message(frameSet(9, 20)); sockets[1].message(preview(20))
  sockets[1].disconnect()
  assert.equal(sockets[0].closed, true)
  reconnects[0](); await settle()
  sockets[2].message(frameSet(1, 1)); sockets[3].message(preview(1))
  sockets[0].message(frameSet(10, 21)); sockets[1].message(preview(21))
  assert.deepEqual(events, ['refresh', 'connect:complete', 'connect:preview',
    'refresh', 'connect:complete', 'connect:preview'])
  assert.deepEqual(frames, [20, 1])
  assert.deepEqual(cursors, [20, 1])
  stop()
})

test('stop closes both lanes, cancels reconnect, and ignores late events', async () => {
  const received: number[] = []
  const { environment, sockets, reconnects } = createEnvironment([])
  const stop = createLiveDisplaySession(async () => undefined, handlers({
    onPreviewEvent: ({ eventCursor }) => received.push(eventCursor),
  }), environment)
  await settle()
  sockets[0].disconnect(); stop(); reconnects[0]()
  sockets[1].message(preview(1)); await settle()
  assert.equal(sockets.length, 2)
  assert.equal(sockets.every(({ closed }) => closed), true)
  assert.deepEqual(received, [])
})
