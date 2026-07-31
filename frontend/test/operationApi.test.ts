import assert from 'node:assert/strict'
import test from 'node:test'

import {
  decodeOperationSnapshot,
  waitForTerminalOperation,
} from '../src/api/operationApi.ts'

const common = {
  operationId: 17,
  submittedAtStateRevision: 4,
}

test('decodes running and succeeded operation snapshots', () => {
  assert.deepEqual(
    decodeOperationSnapshot({ ...common, status: 'Running' }),
    { ...common, status: 'Running' },
  )
  assert.deepEqual(
    decodeOperationSnapshot({ ...common, status: 'Succeeded', frameId: 23 }),
    { ...common, status: 'Succeeded', frameId: 23 },
  )
})

test('rejects unknown statuses and incomplete terminal snapshots', () => {
  assert.throws(
    () => decodeOperationSnapshot({ ...common, status: 'Unknown' }),
    /Invalid operation response/,
  )
  assert.throws(
    () => decodeOperationSnapshot({ ...common, status: 'Succeeded' }),
    /Invalid operation response/,
  )
})

test('accepts every frozen non-success lifecycle status without inventing results', () => {
  for (const status of ['Queued', 'Running', 'CancelRequested', 'Failed', 'Canceled'] as const) {
    assert.deepEqual(decodeOperationSnapshot({ ...common, status }), { ...common, status })
  }
})

test('polls only the operation resource until its first terminal snapshot', async () => {
  const originalFetch = globalThis.fetch
  const calls: Array<{ url: string; cache?: RequestCache }> = []
  let responseNumber = 0
  globalThis.fetch = async (input, init) => {
    calls.push({ url: String(input), cache: init?.cache })
    const status = responseNumber++ === 0 ? 'Running' : 'Succeeded'
    return new Response(JSON.stringify({
      ...common,
      status,
      ...(status === 'Succeeded' ? { frameId: 23 } : {}),
    }), { status: 200 })
  }
  try {
    const result = await waitForTerminalOperation(17, new AbortController().signal)
    assert.deepEqual(result, { ...common, status: 'Succeeded', frameId: 23 })
    assert.deepEqual(calls, [
      { url: '/api/v1/operations/17', cache: 'no-store' },
      { url: '/api/v1/operations/17', cache: 'no-store' },
    ])
  } finally {
    globalThis.fetch = originalFetch
  }
})
