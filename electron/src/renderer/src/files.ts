import type { FileKind } from "@shared/ipc"

/** An entry in the Explorer open-files list. */
export interface FileEntry {
  name: string
  path: string
  kind: FileKind
  size: number
}

/** Analyze paths through the binding and turn each into a real FileEntry. */
export async function ingestPaths(paths: string[]): Promise<FileEntry[]> {
  if (paths.length === 0) return []
  return Promise.all(
    paths.map(async (p) => {
      const s = await window.api.analyze(p)
      return {
        name: s.fileName,
        path: s.path,
        kind: s.kind,
        size: s.filesizeBytes,
      } satisfies FileEntry
    }),
  )
}
