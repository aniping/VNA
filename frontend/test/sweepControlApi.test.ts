import assert from 'node:assert/strict'
import test from 'node:test'
import { startSingleSweep, updateChannelSweepControl } from '../src/api/vnaApi.ts'
function commandResponse(value: unknown = { channelId: 3 }): Response {
  return new Response(JSON.stringify({ status: 'succeeded', stateRevision: 8, value }))
}
test('sends one strictly shaped Continuous or Single control command', async () => {
  const originalFetch = globalThis.fetch
  const originalCrypto = globalThis.crypto
  const requests: RequestInit[] = []
  Object.defineProperty(globalThis, 'crypto', { configurable: true,
    value: { randomUUID: () => 'command-1' } })
  globalThis.fetch = async (_input, init) => { requests.push(init ?? {}); return commandResponse() }
  try {
    await updateChannelSweepControl(7, 3, 'single', 4)
    assert.equal(requests.length, 1)
    assert.deepEqual(JSON.parse(String(requests[0].body)), {
      commandId: 'command-1', sessionId: 'browser-session', instrumentId: 'instrument-1',
      expectedStateRevision: 7, type: 'updateChannelSweepControl',
      payload: { channelId: 3, mode: 'single', sweepCount: 4 },
    })
  } finally {
    globalThis.fetch = originalFetch
    Object.defineProperty(globalThis, 'crypto', { configurable: true, value: originalCrypto })
  }
})
test('Restart emits exactly one startSingleSweep command and validates its operation id', async () => {
  const originalFetch = globalThis.fetch
  const originalCrypto = globalThis.crypto
  const bodies: unknown[] = []
  Object.defineProperty(globalThis, 'crypto', { configurable: true,
    value: { randomUUID: () => 'command-2' } })
  globalThis.fetch = async (_input, init) => {
    bodies.push(JSON.parse(String(init?.body)))
    return commandResponse({ operationId: 17 })
  }
  try {
    const result = await startSingleSweep(9, 3)
    assert.equal(result.value.operationId, 17)
    assert.deepEqual(bodies, [{
      commandId: 'command-2', sessionId: 'browser-session', instrumentId: 'instrument-1',
      expectedStateRevision: 9, type: 'startSingleSweep', payload: { channelId: 3 },
    }])
  } finally {
    globalThis.fetch = originalFetch
    Object.defineProperty(globalThis, 'crypto', { configurable: true, value: originalCrypto })
  }
})
