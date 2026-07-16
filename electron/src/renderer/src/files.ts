import type { FileKind } from "@shared/ipc"

/** An entry in the Explorer open-files list. */
export interface FileEntry {
  name: string
  path: string
  kind: FileKind
  size: number
}

// Repo test fixtures, preloaded so the app is demoable out of the box. Paths are
// repo-relative to the electron/ working directory (where the app is launched);
// the main process resolves them against process.cwd() via the native read().
export const INITIAL_FILES: FileEntry[] = [
  { name: "test.mzML", path: "../test/data/test.mzML", kind: "mzML", size: 4_946_404 },
  {
    name: "sciex_ttof6600_100.mzML",
    path: "../test/data/sciex_ttof6600_100.mzML",
    kind: "mzML",
    size: 909_920,
  },
  { name: "test.msz", path: "../test/data/test.msz", kind: "msz", size: 952_047 },
  { name: "test.mszx", path: "../test/data/mszx/test.mszx", kind: "mszx", size: 931_811 },
]
