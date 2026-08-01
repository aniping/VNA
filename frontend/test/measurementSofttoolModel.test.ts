import assert from 'node:assert/strict'
import test from 'node:test'

import {
  logicalPorts,
  isMeasurementChoiceDisabled,
  measurementCategories,
  physicalPorts,
  portPairForMeasurement,
  sParameters,
} from '../src/components/measurementSofttoolModel.ts'

test('ZNB S-Parameter menu keeps its evidenced control hierarchy', () => {
  assert.deepEqual(sParameters, ['S11', 'S12', 'S21', 'S22'])
  assert.deepEqual(measurementCategories, [
    'S-Params', 'Ratios', 'Wave', 'Z ← Sij', 'Y ← Sij',
    'Y - Z-Params', 'Imbal. CMRR', 'Stability', 'Power Sensor', 'DC',
  ])
  assert.deepEqual(physicalPorts, ['P1', 'P2'])
  assert.deepEqual(logicalPorts, ['L1', 'L2'])
})

test('only a different S-Parameter is selectable for a healthy active Trace', () => {
  assert.equal(isMeasurementChoiceDisabled('S21', 'S11', false, false), false)
  assert.equal(isMeasurementChoiceDisabled('S21', 'S21', false, false), true)
  assert.equal(isMeasurementChoiceDisabled(undefined, 'S11', false, false), true)
  assert.equal(isMeasurementChoiceDisabled('S21', 'S11', true, false), true)
  assert.equal(isMeasurementChoiceDisabled('S21', 'S11', false, true), true)
})

test('the displayed logical port pair follows the authoritative measurement identity', () => {
  assert.equal(portPairForMeasurement('S21'), '21')
  assert.equal(portPairForMeasurement('S12'), '12')
  assert.equal(portPairForMeasurement(undefined), '—')
})
