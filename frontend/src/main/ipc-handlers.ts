import { ipcMain } from 'electron'
import { getWorkerManager } from './worker-manager'

export interface MSCompressAPI {
    getNumThreads: () => Promise<number>
}

export function registerIPCHandlers(): void {
  const workerManager = getWorkerManager()

  ipcMain.handle('mscompress:getNumThreads', async () => {
    try {
      const result = await workerManager.call<number>('getNumThreads')
      return { success: true, data: result }
    } catch (error) {
      return {
        success: false,
        error: error instanceof Error ? error.message : 'Unknown error'
      }
    }
  })
}
