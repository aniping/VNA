import type { SweepRuntimeSnapshot } from '../api/vnaApi.ts'
import type { SweepStreamStatus, SweepUserPhase } from '../api/sweepPreview.ts'
export type SweepSofttoolPage = 'control' | 'trigger' | 'parameters'
export const sweepSofttoolPages = [
  { id: 'control', label: 'Sweep Control' },
  { id: 'trigger', label: 'Trigger In' },
  { id: 'parameters', label: 'Sweep Params' },
] as const
export const sweepControlUnavailableItems = [
  'Restart Manager', 'All Channels Continuous', 'All Channels on Hold',
  'Sweep Controller', 'Pipelining',
] as const
export const triggerSourceItems = ['Free Run', 'External', 'Multiple', 'Manual'] as const
export function parseSweepCount(value: string): number | null {
  const parsed = Number(value.trim())
  return Number.isSafeInteger(parsed) && parsed >= 1 && parsed <= 100_000 ? parsed : null
}
export function parseSweepPoints(value: string): number | null {
  const parsed = Number(value.trim())
  return Number.isSafeInteger(parsed) && parsed >= 2 && parsed <= 0xffff_ffff ? parsed : null
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
export function statusBarChannelLabel(channel: { readonly id: number } | undefined): string {
  // The current state contract has no averaging counter. ZNB p49 and the user-locked
  // baseline define `Avg None` as the honest unavailable-counter label.
  return channel ? `Ch${channel.id}: Avg None` : 'Ch—: Avg None'
}
export function statusBarSweepLabel(
  status: SweepStreamStatus | null,
  phase: SweepRuntimeSnapshot['phase'] | undefined,
): string {
  if (status) return sweepProgressLabel(status)
  return phase ? sweepPhaseLabel(phase) : 'Sweep —'
}
export function sweepBoundaryKey(status: SweepStreamStatus): string | null {
  if (status.userPhase !== 'hold') return null
  return `${status.generation}:${status.sweepId ?? 'none'}:${status.stateRevision}`
}
