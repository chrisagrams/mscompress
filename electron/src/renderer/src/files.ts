import type { FileKind } from "@shared/ipc"

/** An entry in the Explorer open-files list. */
export interface FileEntry {
  name: string
  path: string
  kind: FileKind
  size: number
}

/** Basename of a path, handling both POSIX and Windows separators. */
function basename(path: string): string {
  const parts = path.split(/[/\\]/)
  return parts[parts.length - 1] || path
}

/** File kind from the extension (mirrors kindFromPath in main/bindings.ts). */
function kindFromPath(path: string): FileKind {
  const lower = path.toLowerCase()
  if (lower.endsWith(".msz")) return "msz"
  if (lower.endsWith(".mszx")) return "mszx"
  return "mzML"
}

/**
 * Build cheap FileEntry rows for the Explorer. Only a fast `stat` (getFilesize)
 * per path — the heavy `analyze` runs lazily in the Inspector on selection.
 */
export async function ingestPaths(paths: string[]): Promise<FileEntry[]> {
  if (paths.length === 0) return []
  return Promise.all(
    paths.map(async (p) => {
      const size = await window.api.getFilesize(p)
      return {
        name: basename(p),
        path: p,
        kind: kindFromPath(p),
        size,
      } satisfies FileEntry
    }),
  )
}
