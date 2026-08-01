import { decodeTraceDisplayFrameSet } from './traceDisplayFrameSet.ts'
import type { TraceDisplayFrameSet } from './traceDisplayFrameSet.ts'

export interface DisplayFrameSocketHandlers {
  onOpen(): void
  onMessage(data: unknown): void
  onClose(close: DisplayFrameSocketClose): void
}

export interface DisplayFrameSocketClose {
  readonly code: number
  readonly reason: string
}

export interface DisplayFrameSocket {
  close(): void
}

export interface LiveDisplayEnvironment {
  openSocket(handlers: DisplayFrameSocketHandlers): DisplayFrameSocket
  scheduleReconnect(callback: () => void): () => void
}

export interface LiveDisplayHandlers {
  onFrameSet(frameSet: TraceDisplayFrameSet): void
  onError(error: Error): void
  onConnectionChange(state: LiveDisplayConnection): void
}

export type LiveDisplayConnection = 'connecting' | 'online' | 'offline' | 'unavailable'

const reconnectDelayMs = 500

function displayFramesUrl(): string {
  const url = new URL('/api/v1/display-frames', window.location.href)
  url.protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return url.toString()
}

function browserEnvironment(): LiveDisplayEnvironment {
  return {
    openSocket(handlers) {
      const socket = new WebSocket(displayFramesUrl())
      socket.addEventListener('open', handlers.onOpen, { once: true })
      socket.addEventListener('message', (event) => handlers.onMessage(event.data))
      socket.addEventListener('close', (event) => handlers.onClose({
        code: event.code,
        reason: event.reason,
      }), { once: true })
      return { close: () => socket.close() }
    },
    scheduleReconnect(callback) {
      const timer = globalThis.setTimeout(callback, reconnectDelayMs)
      return () => globalThis.clearTimeout(timer)
    },
  }
}

function messageFrameSet(data: unknown): TraceDisplayFrameSet {
  if (typeof data !== 'string') throw new Error('Display frame-set message must be text')
  return decodeTraceDisplayFrameSet(JSON.parse(data))
}

function reportError(handlers: LiveDisplayHandlers, error: unknown): void {
  handlers.onError(error instanceof Error ? error : new Error('Display frame stream failed'))
}

function frameSetAdvances(
  frameSet: TraceDisplayFrameSet,
  baseline: { generation: number; sequenceNumber: number },
): boolean {
  return frameSet.generation > baseline.generation
    || (frameSet.generation === baseline.generation
      && frameSet.sequenceNumber > baseline.sequenceNumber)
}

class LiveDisplaySession {
  private stopped = false
  private generation = 0
  private socket: DisplayFrameSocket | null = null
  private cancelReconnect: (() => void) | null = null
  private readonly refreshState: () => Promise<void>
  private readonly handlers: LiveDisplayHandlers
  private readonly environment: LiveDisplayEnvironment

  constructor(
    refreshState: () => Promise<void>,
    handlers: LiveDisplayHandlers,
    environment: LiveDisplayEnvironment,
  ) {
    this.refreshState = refreshState
    this.handlers = handlers
    this.environment = environment
  }

  start(): () => void {
    void this.connect()
    return () => this.stop()
  }

  private async connect(): Promise<void> {
    const generation = ++this.generation
    this.handlers.onConnectionChange('connecting')
    try {
      await this.refreshState()
      if (!this.isCurrent(generation)) return
      // This baseline belongs to one socket only. A server restart can reset both counters,
      // while an in-place configuration change advances generation and may reset sequence.
      const baseline = { generation: 0, sequenceNumber: 0 }
      const socket = this.environment.openSocket({
        onOpen: () => this.connected(generation),
        onMessage: (data) => this.receive(generation, baseline, data),
        onClose: () => this.disconnected(generation),
      })
      if (this.isCurrent(generation)) this.socket = socket
      else socket.close()
    } catch (error) {
      if (!this.isCurrent(generation)) return
      reportError(this.handlers, error)
      this.handlers.onConnectionChange('offline')
      this.scheduleReconnect()
    }
  }

  private connected(generation: number): void {
    if (this.isCurrent(generation)) this.handlers.onConnectionChange('online')
  }

  private receive(
    generation: number,
    baseline: { generation: number; sequenceNumber: number },
    data: unknown,
  ): void {
    if (!this.isCurrent(generation)) return
    try {
      const frameSet = messageFrameSet(data)
      if (!frameSetAdvances(frameSet, baseline)) return
      baseline.generation = frameSet.generation
      baseline.sequenceNumber = frameSet.sequenceNumber
      this.handlers.onFrameSet(frameSet)
    } catch (error) {
      reportError(this.handlers, error)
    }
  }

  private disconnected(generation: number): void {
    if (!this.isCurrent(generation)) return
    // The session owns transport only: callers keep their last good frame while this
    // invalidates queued events that could otherwise race a replacement connection.
    this.generation += 1
    this.socket = null
    this.handlers.onConnectionChange('offline')
    this.scheduleReconnect()
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.cancelReconnect) return
    this.cancelReconnect = this.environment.scheduleReconnect(() => {
      this.cancelReconnect = null
      if (!this.stopped) void this.connect()
    })
  }

  private isCurrent(generation: number): boolean {
    return !this.stopped && generation === this.generation
  }

  private stop(): void {
    if (this.stopped) return
    this.stopped = true
    this.generation += 1
    this.cancelReconnect?.()
    this.cancelReconnect = null
    this.socket?.close()
    this.socket = null
  }
}

export function createLiveDisplaySession(
  refreshState: () => Promise<void>,
  handlers: LiveDisplayHandlers,
  environment: LiveDisplayEnvironment,
): () => void {
  return new LiveDisplaySession(refreshState, handlers, environment).start()
}

export function startLiveDisplaySession(
  refreshState: () => Promise<void>,
  handlers: LiveDisplayHandlers,
): () => void {
  return createLiveDisplaySession(refreshState, handlers, browserEnvironment())
}
