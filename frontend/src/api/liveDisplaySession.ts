import { decodeSweepPreviewEvent, type SweepPreviewEvent } from './sweepPreview.ts'
import { decodeTraceDisplayFrameSet, type TraceDisplayFrameSet } from './traceDisplayFrameSet.ts'

export type LiveDisplayStream = 'complete' | 'preview'

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
  openSocket(stream: LiveDisplayStream, handlers: DisplayFrameSocketHandlers): DisplayFrameSocket
  scheduleReconnect(callback: () => void): () => void
}

export interface LiveDisplayHandlers {
  onFrameSet(frameSet: TraceDisplayFrameSet): void
  onPreviewEvent(event: SweepPreviewEvent): void
  onError(error: Error): void
  onConnectionChange(state: LiveDisplayConnection): void
}

export type LiveDisplayConnection = 'connecting' | 'online' | 'offline' | 'unavailable'

const reconnectDelayMs = 500
const streamPaths: Record<LiveDisplayStream, string> = {
  complete: '/api/v1/display-frames',
  preview: '/api/v1/sweep-previews',
}

function streamUrl(stream: LiveDisplayStream): string {
  const url = new URL(streamPaths[stream], window.location.href)
  url.protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return url.toString()
}

function browserEnvironment(): LiveDisplayEnvironment {
  return {
    openSocket(stream, handlers) {
      const socket = new WebSocket(streamUrl(stream))
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

function textJson(data: unknown, stream: LiveDisplayStream): unknown {
  if (typeof data !== 'string') throw new Error(`${stream} display message must be text`)
  return JSON.parse(data)
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
  private connectionGeneration = 0
  private sockets: DisplayFrameSocket[] = []
  private opened = new Set<LiveDisplayStream>()
  private cancelReconnect: (() => void) | null = null

  constructor(
    private readonly refreshState: () => Promise<void>,
    private readonly handlers: LiveDisplayHandlers,
    private readonly environment: LiveDisplayEnvironment,
  ) {}

  start(): () => void {
    void this.connect()
    return () => this.stop()
  }

  private async connect(): Promise<void> {
    const generation = ++this.connectionGeneration
    this.opened = new Set()
    this.handlers.onConnectionChange('connecting')
    try {
      await this.refreshState()
      if (!this.isCurrent(generation)) return
      this.openPair(generation)
    } catch (error) {
      if (!this.isCurrent(generation)) return
      reportError(this.handlers, error)
      this.handlers.onConnectionChange('offline')
      this.scheduleReconnect()
    }
  }

  private openPair(generation: number): void {
    const frameBaseline = { generation: 0, sequenceNumber: 0 }
    const previewBaseline = { eventCursor: 0 }
    const sockets = (['complete', 'preview'] as const).map((stream) => (
      this.environment.openSocket(stream, {
        onOpen: () => this.connected(generation, stream),
        onMessage: (data) => this.receive(generation, stream, data, frameBaseline, previewBaseline),
        onClose: () => this.disconnected(generation),
      })
    ))
    if (this.isCurrent(generation)) this.sockets = sockets
    else sockets.forEach((socket) => socket.close())
  }

  private connected(generation: number, stream: LiveDisplayStream): void {
    if (!this.isCurrent(generation)) return
    this.opened.add(stream)
    if (this.opened.size === 2) this.handlers.onConnectionChange('online')
  }

  private receive(
    generation: number,
    stream: LiveDisplayStream,
    data: unknown,
    frameBaseline: { generation: number; sequenceNumber: number },
    previewBaseline: { eventCursor: number },
  ): void {
    if (!this.isCurrent(generation)) return
    try {
      if (stream === 'complete') {
        const frameSet = decodeTraceDisplayFrameSet(textJson(data, stream))
        if (!frameSetAdvances(frameSet, frameBaseline)) return
        frameBaseline.generation = frameSet.generation
        frameBaseline.sequenceNumber = frameSet.sequenceNumber
        this.handlers.onFrameSet(frameSet)
        return
      }
      const event = decodeSweepPreviewEvent(textJson(data, stream))
      if (event.eventCursor <= previewBaseline.eventCursor) return
      previewBaseline.eventCursor = event.eventCursor
      this.handlers.onPreviewEvent(event)
    } catch (error) {
      reportError(this.handlers, error)
    }
  }

  private disconnected(generation: number): void {
    if (!this.isCurrent(generation)) return
    // Both lanes form one display session. Invalidating the pair prevents a late event from
    // being combined with a baseline established by only the replacement lane.
    this.connectionGeneration += 1
    this.closeSockets()
    this.handlers.onConnectionChange('offline')
    this.scheduleReconnect()
  }

  private closeSockets(): void {
    const sockets = this.sockets
    this.sockets = []
    this.opened.clear()
    sockets.forEach((socket) => socket.close())
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.cancelReconnect) return
    this.cancelReconnect = this.environment.scheduleReconnect(() => {
      this.cancelReconnect = null
      if (!this.stopped) void this.connect()
    })
  }

  private isCurrent(generation: number): boolean {
    return !this.stopped && generation === this.connectionGeneration
  }

  private stop(): void {
    if (this.stopped) return
    this.stopped = true
    this.connectionGeneration += 1
    this.cancelReconnect?.()
    this.cancelReconnect = null
    this.closeSockets()
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
