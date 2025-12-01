import { ipcMain } from 'electron'
import { getWorkerManager } from './worker-manager'

// Define your API methods and their types
export interface MSCompressAPI {
//   compress: (data: Buffer, options?: unknown) => Promise<Buffer>
//   decompress: (data: Buffer, options?: unknown) => Promise<Buffer>
//   getVersion: () => Promise<string>
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

  // Generic RPC handler
//   ipcMain.handle('mscompress:call', async (_event, method: string, ...params: unknown[]) => {
//     try {
//       const result = await workerManager.call(method, ...params)
//       return { success: true, data: result }
//     } catch (error) {
//       return {
//         success: false,
//         error: error instanceof Error ? error.message : 'Unknown error'
//       }
//     }
//   })

//   // Specific handlers for better type safety (optional)
//   ipcMain.handle('mscompress:compress', async (_event, data: Buffer, options?: unknown) => {
//     try {
//       const result = await workerManager.call<Buffer>('compress', data, options)
//       return { success: true, data: result }
//     } catch (error) {
//       return {
//         success: false,
//         error: error instanceof Error ? error.message : 'Unknown error'
//       }
//     }
//   })

//   ipcMain.handle('mscompress:decompress', async (_event, data: Buffer, options?: unknown) => {
//     try {
//       const result = await workerManager.call<Buffer>('decompress', data, options)
//       return { success: true, data: result }
//     } catch (error) {
//       return {
//         success: false,
//         error: error instanceof Error ? error.message : 'Unknown error'
//       }
//     }
//   })

//   ipcMain.handle('mscompress:getVersion', async () => {
//     try {
//       const result = await workerManager.call<string>('getVersion')
//       return { success: true, data: result }
//     } catch (error) {
//       return {
//         success: false,
//         error: error instanceof Error ? error.message : 'Unknown error'
//       }
//     }
//   })
}
