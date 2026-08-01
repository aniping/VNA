import type { LiveDisplayConnection } from '../api/liveDisplaySession.ts'

interface DisplayConfiguration {
  readonly instrument: {
    readonly windows: readonly unknown[]
    readonly traces: readonly unknown[]
  }
}

export interface WorkspacePresentationInput {
  readonly state: DisplayConfiguration | null
  readonly connection: LiveDisplayConnection
  readonly hasFrame: boolean
  readonly displayError: string
}

export interface WorkspacePresentation {
  readonly mode: 'fault' | 'empty' | 'ready' | 'stale'
  readonly showDiagrams: boolean
  readonly controlsDisabled: boolean
  readonly statusLabel: string
  readonly statusTone: LiveDisplayConnection
  readonly headline: string
  readonly detail: string
}

function recoverableControlsDisabled(input: WorkspacePresentationInput): boolean {
  return Boolean(input.displayError)
    || (input.connection !== 'online' && input.connection !== 'unavailable')
}

function withoutState(input: WorkspacePresentationInput): WorkspacePresentation {
  const connecting = input.connection === 'connecting' && !input.displayError
  return {
    mode: 'fault',
    showDiagrams: false,
    controlsDisabled: true,
    statusLabel: connecting ? 'CONNECTING' : 'OFFLINE',
    statusTone: connecting ? 'connecting' : 'offline',
    headline: connecting ? 'Connecting to service' : 'Service unavailable',
    detail: input.displayError || 'Waiting for state and live display data.',
  }
}

function displayStatus(
  input: WorkspacePresentationInput,
): Pick<WorkspacePresentation, 'statusLabel' | 'statusTone'> {
  if (input.connection === 'unavailable') {
    return { statusLabel: 'NO DISPLAY DATA', statusTone: 'unavailable' }
  }
  if (input.connection === 'online') {
    return input.displayError
      ? { statusLabel: 'OFFLINE', statusTone: 'offline' }
      : { statusLabel: 'ONLINE', statusTone: 'online' }
  }
  return input.connection === 'connecting'
    ? { statusLabel: 'CONNECTING', statusTone: 'connecting' }
    : { statusLabel: 'RECONNECTING', statusTone: 'connecting' }
}

function withoutConfiguration(input: WorkspacePresentationInput): WorkspacePresentation {
  return {
    mode: 'empty',
    showDiagrams: false,
    controlsDisabled: recoverableControlsDisabled(input),
    ...displayStatus(input),
    headline: 'No available Diagram',
    detail: 'No display configuration is available.',
  }
}

function configuredWithoutFrame(input: WorkspacePresentationInput): WorkspacePresentation {
  const unavailable = input.connection === 'unavailable'
  return {
    mode: 'fault',
    // Display configuration is authoritative even before samples arrive; DiagramPane owns its grid.
    showDiagrams: true,
    controlsDisabled: recoverableControlsDisabled(input),
    ...displayStatus(input),
    headline: unavailable
      ? 'Display data unavailable'
      : input.connection === 'connecting' ? 'Connecting to service' : 'Service unavailable',
    detail: unavailable
      ? 'Current format display is not supported yet.'
      : input.displayError || 'Waiting for live display data.',
  }
}

export function selectWorkspacePresentation(
  input: WorkspacePresentationInput,
): WorkspacePresentation {
  // An open socket alone is insufficient: a state/frame decoder error means the display is unhealthy.
  const healthy = input.connection === 'online' && !input.displayError
  if (!input.state) return withoutState(input)

  const hasConfiguration = input.state.instrument.windows.length > 0
    && input.state.instrument.traces.length > 0
  if (!hasConfiguration) return withoutConfiguration(input)
  if (input.connection === 'unavailable') return configuredWithoutFrame(input)
  if (!healthy && !input.hasFrame) return configuredWithoutFrame(input)
  if (!healthy) {
    // Reconnection changes only the status presentation; last-good diagrams stay mounted.
    const reconnecting = input.connection !== 'online'
    return {
      mode: 'stale',
      showDiagrams: true,
      controlsDisabled: recoverableControlsDisabled(input),
      statusLabel: reconnecting ? 'RECONNECTING · STALE' : 'DISPLAY ERROR · STALE',
      statusTone: reconnecting ? 'connecting' : 'offline',
      headline: reconnecting ? 'Reconnecting' : 'Display data error',
      detail: 'Showing last received data.',
    }
  }
  return {
    mode: 'ready',
    showDiagrams: true,
    controlsDisabled: false,
    statusLabel: 'ONLINE',
    statusTone: 'online',
    headline: '',
    detail: '',
  }
}
