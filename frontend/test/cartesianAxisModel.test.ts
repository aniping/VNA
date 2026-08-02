import assert from 'node:assert/strict'
import test from 'node:test'

import {
  cartesianAxisTicks,
  formatCartesianAxisTick,
  formatCartesianScaleSummary,
  selectCartesianAxis,
} from '../src/components/cartesianAxisModel.ts'
import type { TraceSnapshot } from '../src/api/vnaApi.ts'

const phaseTrace: TraceSnapshot = {
  id: 1, windowId: 1, measurementId: 1, format: 'phase', scale: null,
}
const logTrace: TraceSnapshot = {
  id: 2, windowId: 1, measurementId: 1, format: 'logMagnitude',
  scale: { scalePerDivision: 10, referenceValue: 0, referencePosition: 9,
    minimum: -90, maximum: 10, unit: 'dB' },
}

test('uses the frozen ZNB Phase viewport independently of the backend wrap domain', () => {
  const axis = selectCartesianAxis(phaseTrace)
  assert.deepEqual(axis, {
    range: { minimum: -225, maximum: 225 },
    unit: 'degree',
    scalePerDivision: 45,
    referenceValue: 0,
    referencePosition: 5,
  })
  assert.deepEqual(cartesianAxisTicks(axis!), [
    225, 180, 135, 90, 45, 0, -45, -90, -135, -180, -225,
  ])
})

test('renders LogMagnitude ticks only from the authoritative Scale snapshot', () => {
  const axis = selectCartesianAxis(logTrace)
  assert.deepEqual(axis, {
    range: { minimum: -90, maximum: 10 },
    unit: 'dB',
    scalePerDivision: 10,
    referenceValue: 0,
    referencePosition: 9,
  })
  assert.deepEqual(cartesianAxisTicks(axis!), [
    10, 0, -10, -20, -30, -40, -50, -60, -70, -80, -90,
  ])
})

test('does not invent a Cartesian axis for Smith', () => {
  assert.equal(selectCartesianAxis({ ...phaseTrace, format: 'smith' }), null)
})

test('formats ZNB axis tokens without synthetic positive signs or long degree units', () => {
  assert.equal(formatCartesianAxisTick(225, 'degree'), '225°')
  assert.equal(formatCartesianAxisTick(0, 'degree'), '0°')
  assert.equal(formatCartesianAxisTick(-45, 'degree'), '-45°')
  assert.equal(formatCartesianAxisTick(10, 'dB'), '10 dB')
  assert.equal(formatCartesianAxisTick(-90, 'dB'), '-90 dB')
  assert.equal(formatCartesianScaleSummary(selectCartesianAxis(phaseTrace)!), '45°/ Ref 0°')
  assert.equal(formatCartesianScaleSummary(selectCartesianAxis(logTrace)!), '10 dB/ Ref 0 dB')
})
