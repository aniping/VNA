export const stimulusKeys = ['Start', 'Stop', 'Center', 'Span'] as const
export type StimulusKey = (typeof stimulusKeys)[number]

export const channelKeys = ['Power / Bw / Avg', 'Sweep'] as const
export type ChannelKey = (typeof channelKeys)[number]

export type HardkeyName =
  | 'Meas'
  | 'Format'
  | 'Scale'
  | 'Trace Config'
  | 'Line'
  | 'Marker'
  | StimulusKey
  | ChannelKey
  | 'Cal'
  | 'Channel Config'
  | 'Trigger'
  | 'Offset / Embed'
  | 'File / Print'
  | 'Setup'
  | 'Tools'
  | 'Display'
  | 'Help'
  | 'Preset'

export interface HardkeyItem {
  label: HardkeyName
  enabled?: boolean
  requiresChannel?: boolean
  requiresTrace?: boolean
  requiresScale?: boolean
  accent?: 'help' | 'preset'
  visual?: 'question'
}

export interface HardkeyGroup {
  title: string
  columns: 2 | 3
  keys: HardkeyItem[]
}

export const hardkeyGroups: HardkeyGroup[] = [
  {
    title: 'Trace',
    columns: 3,
    keys: [
      { label: 'Meas', enabled: true },
      { label: 'Format', enabled: true, requiresTrace: true },
      { label: 'Scale', enabled: true, requiresScale: true },
      { label: 'Trace Config' },
      { label: 'Line' },
      { label: 'Marker' },
    ],
  },
  {
    title: 'Stimulus',
    columns: 2,
    keys: stimulusKeys.map((label) => ({ label, enabled: true, requiresChannel: true })),
  },
  {
    title: 'Channel',
    columns: 3,
    keys: [
      ...channelKeys.map((label) => ({ label, enabled: true, requiresChannel: true })),
      { label: 'Cal' },
      { label: 'Channel Config' },
      { label: 'Trigger' },
      { label: 'Offset / Embed' },
    ],
  },
  {
    title: 'System',
    columns: 3,
    keys: [
      { label: 'File / Print' },
      { label: 'Setup' },
      { label: 'Tools' },
      { label: 'Display' },
      { label: 'Help', accent: 'help', visual: 'question' },
      { label: 'Preset', accent: 'preset' },
    ],
  },
]

export function isStimulusKey(key: HardkeyName): key is StimulusKey {
  return stimulusKeys.some((candidate) => candidate === key)
}

export function isChannelKey(key: HardkeyName): key is ChannelKey {
  return channelKeys.some((candidate) => candidate === key)
}
