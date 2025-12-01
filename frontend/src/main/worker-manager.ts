import { Worker } from 'worker_threads'
import { join } from 'path'
import { app } from 'electron'

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

export class WorkerManager {
  private worker: Worker | null = null
  private isReady = false
  private pendingRequests = new Map<
    string,
    { resolve: (value: unknown) => void; reject: (error: Error) => void; timeout?: NodeJS.Timeout }
  >()
  private requestIdCounter = 0

  async initialize(): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        // Path to compiled worker
        const workerPath = app.isPackaged
          ? join(process.resourcesPath, 'app.asar.unpacked', 'out', 'main', 'worker.js')
          : join(__dirname, 'worker.js')

        this.worker = new Worker(workerPath)

        this.worker.on('message', (message: RPCResponse | { ready: boolean }) => {
          // Handle ready signal
          if ('ready' in message && message.ready) {
            this.isReady = true
            resolve()
            return
          }

          // Handle RPC response
          const response = message as RPCResponse
          const pending = this.pendingRequests.get(response.id)
          
          if (pending) {
            this.pendingRequests.delete(response.id)
            
            // Clear timeout
            if (pending.timeout) {
              clearTimeout(pending.timeout)
            }
            
            if (response.error) {
              pending.reject(new Error(response.error.message))
            } else {
              pending.resolve(response.result)
            }
          }
        })

        this.worker.on('error', (error) => {
          console.error('Worker error:', error)
          reject(error)
        })

        this.worker.on('exit', (code) => {
          if (code !== 0) {
            console.error(`Worker stopped with exit code ${code}`)
          }
          this.isReady = false
        })
      } catch (error) {
        reject(error)
      }
    })
  }

  async call<T = unknown>(method: string, ...params: unknown[]): Promise<T> {
    if (!this.isReady || !this.worker) {
      throw new Error('Worker not initialized')
    }

    return new Promise<T>((resolve, reject) => {
      const id = `rpc_${++this.requestIdCounter}_${Date.now()}`
      
      this.pendingRequests.set(id, { 
        resolve: resolve as (value: unknown) => void, 
        reject 
      })

      const request: RPCRequest = {
        id,
        method,
        params
      }

      this.worker!.postMessage(request)

      // Timeout after 30 seconds
      const timeoutId = setTimeout(() => {
        if (this.pendingRequests.has(id)) {
          this.pendingRequests.delete(id)
          reject(new Error(`RPC timeout for method: ${method}`))
        }
      }, 30000)

      // Store timeout for cleanup
      const pending = this.pendingRequests.get(id)
      if (pending) {
        pending.timeout = timeoutId
      }
    })
  }

  async terminate(): Promise<void> {
    if (this.worker) {
      await this.worker.terminate()
      this.worker = null
      this.isReady = false
      this.pendingRequests.clear()
    }
  }
}

// Singleton instance
let workerManager: WorkerManager | null = null

export function getWorkerManager(): WorkerManager {
  if (!workerManager) {
    workerManager = new WorkerManager()
  }
  return workerManager
}
