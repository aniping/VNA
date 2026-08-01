import assert from 'node:assert/strict'
import test from 'node:test'

import { setTraceMeasurementType } from '../src/api/vnaApi.ts'

test('posts one typed command for each different two-port S-Parameter', async () => {
  const originalFetch = globalThis.fetch
  const requests: { input: string; init?: RequestInit }[] = []
  globalThis.fetch = async (input, init) => {
    requests.push({ input: String(input), init })
    return new Response(JSON.stringify({
      status: 'succeeded', stateRevision: 8, value: { traceId: 17 },
    }))
  }
  try {
    const types = ['S11', 'S12', 'S22'] as const
    for (const [index, measurementType] of types.entries()) {
      const result = await setTraceMeasurementType(7, 17, measurementType)
      assert.equal(result.stateRevision, 8)
      assert.equal(requests.length, index + 1)
      assert.equal(requests[index].input, '/api/v1/commands')
      assert.equal(requests[index].init?.method, 'POST')
      const body = JSON.parse(String(requests[index].init?.body))
      assert.equal(typeof body.commandId, 'string')
      assert.deepEqual({ ...body, commandId: '<generated>' }, {
        commandId: '<generated>',
        sessionId: 'browser-session',
        instrumentId: 'instrument-1',
        expectedStateRevision: 7,
        type: 'setTraceMeasurementType',
        payload: { traceId: 17, measurementType },
      })
    }
  } finally {
    globalThis.fetch = originalFetch
  }
})

test('surfaces a rejected trace measurement command without retrying', async () => {
  const originalFetch = globalThis.fetch
  let requestCount = 0
  globalThis.fetch = async () => {
    requestCount += 1
    return new Response('{"error":"revisionConflict"}', { status: 422 })
  }
  try {
    await assert.rejects(setTraceMeasurementType(7, 17, 'S22'), /HTTP 422/)
    assert.equal(requestCount, 1)
  } finally {
    globalThis.fetch = originalFetch
  }
})
