import { contextBridge, ipcRenderer } from 'electron'
import { electronAPI } from '@electron-toolkit/preload'

interface RPCResult<T> {
  success: boolean
  data?: T
  error?: string
}

// Custom APIs for renderer
const api = {
  mscompress: {
    getNumThreads: async (): Promise<number> => {
      const result = await ipcRenderer.invoke('mscompress:getNumThreads') as RPCResult<number>
      if (!result.success) {
        throw new Error(result.error || 'Unknown error')
      }
      return result.data as number
    }
  }
}

// Use `contextBridge` APIs to expose Electron APIs to
// renderer only if context isolation is enabled, otherwise
// just add to the DOM global.
if (process.contextIsolated) {
  try {
    contextBridge.exposeInMainWorld('electron', electronAPI)
    contextBridge.exposeInMainWorld('api', api)
  } catch (error) {
    console.error(error)
  }
} else {
  // @ts-ignore (define in dts)
  window.electron = electronAPI
  // @ts-ignore (define in dts)
  window.api = api
}
