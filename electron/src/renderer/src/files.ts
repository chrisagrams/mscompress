import type { FileKind } from "@shared/ipc"

/** An entry in the Explorer open-files list. */
export interface FileEntry {
  name: string
  path: string
  kind: FileKind
  size: number
}
