import type { SweepRuntimeSnapshot } from '../api/vnaApi.ts'
import type { SweepStreamStatus, SweepUserPhase } from '../api/sweepPreview.ts'
export type SweepSofttoolPage = 'control' | 'trigger' | 'parameters' | 'type' | 'trigger-out'
export const sweepSofttoolPages = [
  { id: 'parameters', label: 'Sweep Params', page: 'parameters' },
  { id: 'type', label: 'Sweep Type', page: 'type' },
  { id: 'trigger', label: 'Trigger In', page: 'trigger' },
  { id: 'trigger-out', label: 'Trigger Out', page: 'trigger-out' },
  { id: 'control', label: 'Sweep Control', page: 'control' },
] as const
export const sweepParameterRows = [
  { label: 'Freq Step Size', value: '—' },
  { label: 'Sweep Time', value: '—' },
  { label: 'Auto', value: '—' },
  { label: 'Meas Delay', value: '—' },
] as const
export const sweepTypeItems = [
  'Lin Freq', 'Log Freq', 'Segmented', 'Power', 'CW Mode', 'Time',
] as const
export const triggerInRows = [
  { label: 'Sequence', value: 'Sweep' },
  { label: 'Delay', value: '0 s' },
  { label: 'Signal Type', value: '—' },
] as const
export const triggerOutRows = [
  { label: 'Trigger Out Active', value: 'Off' },
  { label: 'Interval', value: '—' },
  { label: 'Polarity', value: '—' },
  { label: 'Duration', value: '—' },
  { label: 'Position', value: '—' },
] as const
export function nextSweepPage(page: SweepSofttoolPage): SweepSofttoolPage {
  const index = sweepSofttoolPages.findIndex((item) => item.page === page)
  return sweepSofttoolPages[(index + 1) % sweepSofttoolPages.length].page
}
export const sweepControlUnavailableItems = [
  'Restart Manager', 'All Channels Continuous', 'All Channels on Hold',
  'Sweep Controller', 'Pipelining',
] as const
export const triggerSourceItems = ['Free Run', 'External', 'Multiple', 'Manual'] as const
export interface StatusBarProgress {
  readonly label: string
  readonly percent: number
  readonly firstSweep: boolean
}
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
export function statusBarChannelLabel(channel: { readonly id: number } | undefined): string {
  // The current state contract has no averaging counter. ZNB p49 and the user-locked
  // baseline define `Avg None` as the honest unavailable-counter label.
  return channel ? `Ch${channel.id}: Avg None` : 'Ch—: Avg None'
}
export function statusBarProgress(
  status: SweepStreamStatus | null,
  phase: SweepRuntimeSnapshot['phase'] | undefined,
): StatusBarProgress {
  if (!status) return {
    label: phase ? sweepPhaseLabel(phase) : 'Sweep —', percent: 0, firstSweep: false,
  }
  const { completedAcquisitionPoints: completed, totalAcquisitionPoints: total } = status.progress
  const percent = total > 0 ? Math.min(100, Math.floor(completed * 100 / total)) : 0
  const label = status.userPhase === 'sweeping'
    ? `Ch${status.channelId} ${percent}%` : sweepPhaseLabel(status.userPhase)
  return { label, percent, firstSweep: status.firstSweepAfterConfiguration }
}
export function sweepBoundaryKey(status: SweepStreamStatus): string | null {
  if (status.userPhase !== 'hold') return null
  return `${status.generation}:${status.sweepId ?? 'none'}:${status.stateRevision}`
}
