import { decodeTraceDisplayFrame } from './traceDisplayFrame.ts'
import type { TraceDisplayFrame } from './traceDisplayFrame.ts'

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
  onFrame(frame: TraceDisplayFrame): void
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

function messageFrame(data: unknown): TraceDisplayFrame {
  if (typeof data !== 'string') throw new Error('Display frame message must be text')
  return decodeTraceDisplayFrame(JSON.parse(data))
}

function reportError(handlers: LiveDisplayHandlers, error: unknown): void {
  handlers.onError(error instanceof Error ? error : new Error('Display frame stream failed'))
}

function displayTraceUnavailable(close: DisplayFrameSocketClose): boolean {
  // PolicyViolation also reports capacity errors, so the server-owned reason is required.
  return close.code === 1008 && close.reason === 'display trace unavailable'
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
      // Sequence belongs to one socket generation. A restarted server may begin at one,
      // so carrying the prior connection's baseline would discard its retained latest frame.
      const sequences = new Map<number, number>()
      const socket = this.environment.openSocket({
        onOpen: () => this.connected(generation),
        onMessage: (data) => this.receive(generation, sequences, data),
        onClose: (close) => this.disconnected(generation, close),
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

  private receive(generation: number, sequences: Map<number, number>, data: unknown): void {
    if (!this.isCurrent(generation)) return
    try {
      const frame = messageFrame(data)
      const previous = sequences.get(frame.traceId) ?? 0
      if (frame.sequenceNumber <= previous) return
      sequences.set(frame.traceId, frame.sequenceNumber)
      this.handlers.onFrame(frame)
    } catch (error) {
      reportError(this.handlers, error)
    }
  }

  private disconnected(generation: number, close: DisplayFrameSocketClose): void {
    if (!this.isCurrent(generation)) return
    // The session owns transport only: callers keep their last good frame while this
    // invalidates queued events that could otherwise race a replacement connection.
    this.generation += 1
    this.socket = null
    if (displayTraceUnavailable(close)) {
      this.handlers.onConnectionChange('unavailable')
      return
    }
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
