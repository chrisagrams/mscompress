/// <reference types="vite/client" />
import type { Api } from "@shared/ipc"

declare global {
  interface Window {
    /** Typed IPC bridge exposed by the preload (see src/preload/index.ts). */
    api: Api
  }
}
