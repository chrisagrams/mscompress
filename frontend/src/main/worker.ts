import { parentPort } from 'worker_threads'

import mscompress from '../../../node/build/Release/mscompress.node';

interface RPCRequest {
  id: string
  method: string
  params: unknown[]
}

interface RPCResponse {
  id: string
  result?: unknown
  error?: {
    code: number
    message: string
  }
}

const handlers = {
    getNumThreads: async (): Promise<number> => {
        return mscompress.getNumThreads();
    }
}

type HandlerMethod = keyof typeof handlers

// Process RPC requests
if (parentPort) {
  parentPort.on('message', async (request: RPCRequest) => {
    const response: RPCResponse = { id: request.id }

    try {
      // Validate method exists
      if (!(request.method in handlers)) {
        throw new Error(`Unknown method: ${request.method}`)
      }

      // Call the handler
      const handler = handlers[request.method as HandlerMethod] as (...args: unknown[]) => Promise<unknown>
      const result = await handler(...request.params)
      
      response.result = result
    } catch (error) {
      response.error = {
        code: -1,
        message: error instanceof Error ? error.message : 'Unknown error'
      }
    }

    parentPort!.postMessage(response)
  })

  // Signal ready
  parentPort.postMessage({ ready: true })
}
