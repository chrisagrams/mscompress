// Shared helpers for the binding wrappers: PSI-MS accession labels, lossy
// target formats, file-kind detection and opening.
import { extname } from "path"
import { MSZXFile, read } from "mscompress"
import type { BaseFile } from "mscompress"
import type { FileKind, LossyAlgo, MsLevelCount } from "../../shared/ipc"

// PSI-MS accession codes → human-readable labels (mirrors the C core).
export const MZ_FMT: Record<number, string> = {
  1000519: "int32",
  1000520: "float16",
  1000521: "float32",
  1000522: "int64",
  1000523: "float64",
}
export const COMPRESSION: Record<number, string> = {
  1000574: "zlib",
  1000576: "none",
  4700001: "zstd",
}

// Lossy algorithm → target-format accession code (from src/mscompress.h). "none"
// keeps the default lossless ZSTD target.
export const LOSSY_FORMAT: Record<LossyAlgo, number> = {
  none: 4700001, // _ZSTD_compression_ (lossless)
  cast: 4700002, // _cast_64_to_32_
  log: 4700003, // _log2_transform_
  delta16: 4700004, // _delta16_transform_
  delta32: 4700006, // _delta32_transform_
  vbr: 4700007, // _vbr_
}

export function fmtLabel(map: Record<number, string>, code: number): string | null {
  return map[code] ?? (code ? `code:${code}` : null)
}

export function kindFromPath(path: string): FileKind {
  const ext = extname(path).toLowerCase()
  if (ext === ".msz") return "msz"
  if (ext === ".mszx") return "mszx"
  return "mzML"
}

/** Open a file by kind: mszx uses MSZXFile.open (read() can't detect the tar). */
export function openFile(path: string, kind: FileKind): BaseFile {
  return kind === "mszx" ? MSZXFile.open(path) : read(path)
}

/** Group per-spectrum MS levels into ordered MS1/MS2/MSn counts. */
export function msLevelCounts(levels: Uint16Array): MsLevelCount[] {
  let ms1 = 0
  let ms2 = 0
  let msn = 0
  for (const lvl of levels) {
    if (lvl >= 3) msn++
    else if (lvl === 2) ms2++
    else ms1++
  }
  const out: MsLevelCount[] = []
  if (ms1) out.push({ level: "MS1", count: ms1 })
  if (ms2) out.push({ level: "MS2", count: ms2 })
  if (msn) out.push({ level: "MSn", count: msn })
  return out
}

export function errorMessage(err: unknown): string {
  return err instanceof Error ? err.message : String(err)
}
