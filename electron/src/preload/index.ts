import { contextBridge, ipcRenderer, webUtils } from 'electron'
import type { IpcRendererEvent } from 'electron'
import { IPC, type Api, type ConvertProgress } from '../shared/ipc'

// Typed, channel-whitelisted bridge. The renderer can ONLY reach the main
// process through these explicit methods — no raw ipcRenderer, no channel
// strings from renderer code.
const api: Api = {
  openFiles: () => ipcRenderer.invoke(IPC.openFiles),
  openOutputDir: () => ipcRenderer.invoke(IPC.openOutputDir),
  openExternal: (url) => ipcRenderer.invoke(IPC.openExternal, url),
  revealInFolder: (path) => ipcRenderer.invoke(IPC.revealInFolder, path),
  getDefaultOutputDir: () => ipcRenderer.invoke(IPC.getDefaultOutputDir),
  getVersion: () => ipcRenderer.invoke(IPC.getVersion),
  getNumThreads: () => ipcRenderer.invoke(IPC.getNumThreads),
  getFilesize: (path) => ipcRenderer.invoke(IPC.getFilesize, path),
  analyze: (path) => ipcRenderer.invoke(IPC.analyze, path),
  computeQC: (path, opts) => ipcRenderer.invoke(IPC.computeQC, path, opts),
  compress: (path, opts) => ipcRenderer.invoke(IPC.compress, path, opts),
  decompress: (path, opts) => ipcRenderer.invoke(IPC.decompress, path, opts),
  extract: (path, opts) => ipcRenderer.invoke(IPC.extract, path, opts),
  onConvertProgress: (cb) => {
    const listener = (_e: IpcRendererEvent, p: ConvertProgress): void => cb(p)
    ipcRenderer.on(IPC.convertProgress, listener)
    return () => ipcRenderer.removeListener(IPC.convertProgress, listener)
  },
  getPathForFile: (file) => webUtils.getPathForFile(file)
}

if (process.contextIsolated) {
  contextBridge.exposeInMainWorld('api', api)
} else {
  // contextIsolation is always on in this app; this branch is a dev safety net.
  ;(globalThis as unknown as { api: Api }).api = api
}
