import { ElectronAPI } from '@electron-toolkit/preload'

interface MSCompressAPI {
  getNumThreads: () => Promise<number>
}

interface API {
  nativeCall: <T = unknown>(method: string, ...params: unknown[]) => Promise<T>
  mscompress: MSCompressAPI
}

declare global {
  interface Window {
    electron: ElectronAPI
    api: API
  }
}
