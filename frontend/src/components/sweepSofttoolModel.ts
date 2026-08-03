import type { SweepMode, SweepRuntimeSnapshot } from '../api/vnaApi.ts'
import type { SweepStreamStatus, SweepUserPhase } from '../api/sweepPreview.ts'
export type SweepSofttoolPage = 'parameters' | 'trigger' | 'control'
export const sweepSofttoolPages = [
  { id: 'parameters', label: 'Sweep Params' },
  { id: 'trigger', label: 'Trigger In' },
  { id: 'control', label: 'Sweep Control' },
] as const
export function parseSweepCount(value: string): number | null {
  const parsed = Number(value.trim())
  return Number.isSafeInteger(parsed) && parsed >= 1 && parsed <= 100_000 ? parsed : null
}
export function parseSweepPoints(value: string): number | null {
  const parsed = Number(value.trim())
  return Number.isSafeInteger(parsed) && parsed >= 2 && parsed <= 0xffff_ffff ? parsed : null
}
export function sweepModeLabel(mode: SweepMode): string {
  return mode === 'continuous' ? 'Continuous' : 'Single'
}
export function sweepPhaseLabel(phase: SweepUserPhase): string {
  const labels: Record<SweepUserPhase, string> = {
    hold: 'Hold',
    preparing: 'Preparing',
    sweeping: 'Sweeping',
    calculation: 'Calculation',
    failed: 'Failed',
  }
  return labels[phase]
}
export function sweepProgressLabel(status: SweepStreamStatus | null): string {
  if (!status) return ''
  const { completedAcquisitionPoints: completed, totalAcquisitionPoints: total } = status.progress
  const progress = total > 0 ? ` ${completed}/${total} (${Math.floor(completed * 100 / total)}%)` : ''
  const firstSweep = status.firstSweepAfterConfiguration ? ' *' : ''
  return `${sweepPhaseLabel(status.userPhase)}${progress}${firstSweep}`
}
export function sweepExecutionLabel(
  kind: 'Configured' | 'Applied',
  execution: SweepRuntimeSnapshot['configured'] | SweepRuntimeSnapshot['applied'],
): string {
  return `${kind} ${sweepModeLabel(execution.mode)} ×${execution.sweepCount}`
}
