import assert from 'node:assert/strict'
import test from 'node:test'

import { ensureAllSParameters } from '../src/api/vnaApi.ts'

test('posts one typed ensure-all command for the active Trace', async () => {
  const originalFetch = globalThis.fetch
  const requests: { input: string; init?: RequestInit }[] = []
  globalThis.fetch = async (input, init) => {
    requests.push({ input: String(input), init })
    return new Response(JSON.stringify({
      status: 'succeeded', stateRevision: 8, value: {},
    }))
  }
  try {
    const result = await ensureAllSParameters(7, 17)
    assert.equal(result.stateRevision, 8)
    assert.equal(requests.length, 1)
    const body = JSON.parse(String(requests[0].init?.body))
    assert.deepEqual({ ...body, commandId: '<generated>' }, {
      commandId: '<generated>',
      sessionId: 'browser-session',
      instrumentId: 'instrument-1',
      expectedStateRevision: 7,
      type: 'ensureAllSParameters',
      payload: { traceId: 17 },
    })
  } finally {
    globalThis.fetch = originalFetch
  }
})

test('surfaces a rejected ensure-all command without retrying', async () => {
  const originalFetch = globalThis.fetch
  let requestCount = 0
  globalThis.fetch = async () => {
    requestCount += 1
    return new Response('{}', { status: 422 })
  }
  try {
    await assert.rejects(ensureAllSParameters(7, 17), /HTTP 422/)
    assert.equal(requestCount, 1)
  } finally {
    globalThis.fetch = originalFetch
  }
})
