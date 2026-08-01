import type { MeasurementType } from '../api/vnaApi'

export const sParameters = ['S11', 'S12', 'S21', 'S22'] as const
// The current instrument domain is two-port. Showing P3/P4 from a larger ZNB option would
// advertise unsupported topology, so the visual model stops at the authoritative capability.
export const physicalPorts = ['P1', 'P2'] as const
export const logicalPorts = ['L1', 'L2'] as const

export const measurementCategories = [
  'S-Params',
  'Ratios',
  'Wave',
  'Z ← Sij',
  'Y ← Sij',
  'Y - Z-Params',
  'Imbal. CMRR',
  'Stability',
  'Power Sensor',
  'DC',
] as const

export function portPairForMeasurement(type: MeasurementType | undefined): string {
  return type?.slice(1) ?? '—'
}

export function isMeasurementChoiceDisabled(
  current: MeasurementType | undefined,
  candidate: MeasurementType,
  controlsDisabled: boolean,
  busy: boolean,
): boolean {
  // Keeping the selected item natively disabled makes the no-op rule work for mouse and keyboard.
  return !current || current === candidate || controlsDisabled || busy
}
