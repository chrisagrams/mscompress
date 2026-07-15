import { contextBridge } from 'electron'

// Minimal, channel-whitelisted bridge. Real IPC (native mscompress bindings)
// arrives in a later task; for now we only surface process versions.
const api = {
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node
  }
}

if (process.contextIsolated) {
  try {
    contextBridge.exposeInMainWorld('mscompress', api)
  } catch (error) {
    console.error(error)
  }
} else {
  // @ts-expect-error — fallback when contextIsolation is disabled (not used here)
  window.mscompress = api
}

export type MscompressApi = typeof api
