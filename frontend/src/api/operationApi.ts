export type OperationStatus =
  | 'Queued'
  | 'Running'
  | 'CancelRequested'
  | 'Succeeded'
  | 'Failed'
  | 'Canceled'

interface OperationBase {
  readonly operationId: number
  readonly submittedAtStateRevision: number
}

export type OperationSnapshot = OperationBase & (
  | { readonly status: 'Queued' }
  | { readonly status: 'Running' }
  | { readonly status: 'CancelRequested' }
  | { readonly status: 'Succeeded'; readonly frameId: number }
  | { readonly status: 'Failed' }
  | { readonly status: 'Canceled' }
)

export type TerminalOperationSnapshot = Extract<
  OperationSnapshot,
  { readonly status: 'Succeeded' | 'Failed' | 'Canceled' }
>

type JsonObject = Record<string, unknown>

// 250 ms keeps a one-shot toolbar action responsive without turning lifecycle REST
// into high-frequency data polling. Thirty seconds is this first slice's UI safety
// ceiling, not a guess that an instrument Operation has completed.
const pollIntervalMs = 250
const operationTimeoutMs = 30_000

function invalidOperation(reason: string): never {
  throw new Error(`Invalid operation response: ${reason}`)
}

function isObject(value: unknown): value is JsonObject {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function isSafeInteger(value: unknown, minimum: number): value is number {
  return typeof value === 'number' && Number.isSafeInteger(value) && value >= minimum
}

export function decodeOperationSnapshot(body: unknown): OperationSnapshot {
  if (!isObject(body)) invalidOperation('body is not an object')
  if (!isSafeInteger(body.operationId, 1)) invalidOperation('invalid operation identity')
  if (!isSafeInteger(body.submittedAtStateRevision, 0)) {
    invalidOperation('invalid submission revision')
  }
  const base = {
    operationId: body.operationId,
    submittedAtStateRevision: body.submittedAtStateRevision,
  }
  if (body.status === 'Succeeded') {
    if (!isSafeInteger(body.frameId, 1)) invalidOperation('succeeded operation needs a frame')
    return { ...base, status: body.status, frameId: body.frameId }
  }
  if (
    body.status === 'Queued'
    || body.status === 'Running'
    || body.status === 'CancelRequested'
    || body.status === 'Failed'
    || body.status === 'Canceled'
  ) {
    return { ...base, status: body.status }
  }
  return invalidOperation('unknown status')
}

async function fetchOperation(
  operationId: number,
  signal: AbortSignal,
): Promise<OperationSnapshot> {
  const response = await fetch(`/api/v1/operations/${operationId}`, {
    cache: 'no-store',
    signal,
  })
  if (response.status !== 200) throw new Error(`HTTP ${response.status}`)
  return decodeOperationSnapshot(await response.json())
}

function isTerminal(operation: OperationSnapshot): operation is TerminalOperationSnapshot {
  return operation.status === 'Succeeded'
    || operation.status === 'Failed'
    || operation.status === 'Canceled'
}

function wait(milliseconds: number, signal: AbortSignal): Promise<void> {
  if (signal.aborted) return Promise.reject(signal.reason)
  return new Promise((resolve, reject) => {
    const onElapsed = () => {
      signal.removeEventListener('abort', onAbort)
      resolve()
    }
    const onAbort = () => {
      globalThis.clearTimeout(timer)
      reject(signal.reason)
    }
    const timer = globalThis.setTimeout(onElapsed, milliseconds)
    signal.addEventListener('abort', onAbort, { once: true })
  })
}

export async function waitForTerminalOperation(
  operationId: number,
  signal: AbortSignal,
): Promise<TerminalOperationSnapshot> {
  // Teardown cancellation and the hard deadline share one signal, bounding both
  // a slow HTTP request and the number of lifecycle polls.
  const timeoutSignal = AbortSignal.timeout(operationTimeoutMs)
  const pollingSignal = AbortSignal.any([signal, timeoutSignal])
  try {
    while (true) {
      // Operation is the authoritative completion fence. Polling frames could confuse
      // an older retained frame with completion and would turn data into lifecycle truth.
      const operation = await fetchOperation(operationId, pollingSignal)
      if (operation.operationId !== operationId) invalidOperation('operation identity mismatch')
      if (isTerminal(operation)) return operation
      await wait(pollIntervalMs, pollingSignal)
    }
  } catch (error) {
    if (timeoutSignal.aborted && !signal.aborted) {
      throw new Error('Sweep operation timed out')
    }
    throw error
  }
}
