import { contextBridge, ipcRenderer, webUtils } from 'electron'
import { IPC, type Api } from '../shared/ipc'

// Typed, channel-whitelisted bridge. The renderer can ONLY reach the main
// process through these explicit methods — no raw ipcRenderer, no channel
// strings from renderer code.
const api: Api = {
  openFiles: () => ipcRenderer.invoke(IPC.openFiles),
  openOutputDir: () => ipcRenderer.invoke(IPC.openOutputDir),
  openExternal: (url) => ipcRenderer.invoke(IPC.openExternal, url),
  getDefaultOutputDir: () => ipcRenderer.invoke(IPC.getDefaultOutputDir),
  getVersion: () => ipcRenderer.invoke(IPC.getVersion),
  getNumThreads: () => ipcRenderer.invoke(IPC.getNumThreads),
  getFilesize: (path) => ipcRenderer.invoke(IPC.getFilesize, path),
  analyze: (path) => ipcRenderer.invoke(IPC.analyze, path),
  getPathForFile: (file) => webUtils.getPathForFile(file)
}

if (process.contextIsolated) {
  contextBridge.exposeInMainWorld('api', api)
} else {
  // contextIsolation is always on in this app; this branch is a dev safety net.
  ;(globalThis as unknown as { api: Api }).api = api
}
