import assert from 'node:assert/strict'
import test from 'node:test'

import { traceColorForTrace } from '../src/components/traceVisual.ts'

test('stable Trace identities select the project palette with factory Trc1 green', () => {
  assert.equal(traceColorForTrace(1), '#54d454')
  assert.equal(traceColorForTrace(2), '#f2db24')
  assert.equal(traceColorForTrace(3), '#36c5d8')
  assert.equal(traceColorForTrace(4), '#d879e8')
  assert.equal(traceColorForTrace(1), '#54d454')
  assert.equal(traceColorForTrace(undefined), '#f2db24')
})
