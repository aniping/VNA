import assert from 'node:assert/strict'
import test from 'node:test'

import { traceColorForMeasurement } from '../src/components/traceVisual.ts'

test('manual-default S21 uses green while other Traces retain the existing first color', () => {
  assert.equal(traceColorForMeasurement('S21'), '#54d454')
  assert.equal(traceColorForMeasurement('S11'), '#f2db24')
  assert.equal(traceColorForMeasurement(undefined), '#f2db24')
})
