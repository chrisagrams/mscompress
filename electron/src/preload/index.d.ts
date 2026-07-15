import type { MscompressApi } from './index'

declare global {
  interface Window {
    mscompress: MscompressApi
  }
}
