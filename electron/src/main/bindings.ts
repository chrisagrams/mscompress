// Thin main-process wrapper around the native `mscompress` node-ts binding.
// This module is the ONLY place the addon is required, and it must never be
// imported from the renderer (it is externalized from the Vite bundle).
import { createRequire } from 'module'
import { basename, extname } from 'path'
import type { FileKind, FileSummary } from '../shared/ipc'

// The binding ships as ESM; require(ESM) (Node 22.12+/Electron 38) loads its
// namespace synchronously, which is all we need for these cheap header reads.
const require = createRequire(import.meta.url)

// The binding's public surface we use here. Typed loosely to avoid pulling the
// package's ESM type graph into the CJS main bundle; the shapes are stable.
interface MscompressModule {
  getVersion(): string
  getNumThreads(): number
  getFilesize(filePath: string): number
  read(filePath: string): MsFile
}

interface MsDataFormat {
  sourceMzFmt: number
  sourceIntenFmt: number
  sourceCompression: number
  sourceTotalSpec: number
  toDict(): Record<string, string | number>
}

interface MsFile {
  path: string
  filesize: number | bigint
  format: MsDataFormat
  close(): void
}

let cached: MscompressModule | null = null

/** Lazily load the native addon (deferred so the app can start even if the
 * binding is temporarily unavailable, and to keep startup cheap). */
function mod(): MscompressModule {
  if (!cached) {
    cached = require('mscompress') as MscompressModule
  }
  return cached
}

/** Coerce a possibly-BigInt native size to a plain, IPC-safe number. */
function toNumber(v: number | bigint): number {
  return typeof v === 'bigint' ? Number(v) : v
}

// PSI-MS accession codes → human-readable labels (mirrors the C core).
const MZ_FMT: Record<number, string> = {
  1000519: 'int32',
  1000520: 'float16',
  1000521: 'float32',
  1000522: 'int64',
  1000523: 'float64'
}
const COMPRESSION: Record<number, string> = {
  1000574: 'zlib',
  1000576: 'none',
  4700001: 'zstd'
}

function fmtLabel(map: Record<number, string>, code: number): string | null {
  return map[code] ?? (code ? `code:${code}` : null)
}

function kindFromPath(path: string): FileKind {
  const ext = extname(path).toLowerCase()
  if (ext === '.msz') return 'msz'
  if (ext === '.mszx') return 'mszx'
  return 'mzML'
}

export function getVersion(): string {
  return mod().getVersion()
}

export function getNumThreads(): number {
  return mod().getNumThreads()
}

export function getFilesize(path: string): number {
  return toNumber(mod().getFilesize(path))
}

/**
 * Analyze a file and return a plain, JSON-serializable summary. For mzML/msz
 * this opens the file via the native `read()` and reads the header DataFormat;
 * for mszx (a tar archive not handled by `read()`) it returns size-only info.
 * Always resolves — failures are reported in the `error` field.
 */
export function analyze(path: string): FileSummary {
  const kind = kindFromPath(path)
  const base: FileSummary = {
    path,
    fileName: basename(path),
    kind,
    filesizeBytes: 0,
    spectrumCount: null,
    mzFormat: null,
    intensityFormat: null,
    sourceCompression: null,
    accessions: null
  }

  try {
    if (kind === 'mszx') {
      // read() doesn't decode the archive container; report size only for now.
      base.filesizeBytes = getFilesize(path)
      return base
    }

    const file = mod().read(path)
    try {
      const fmt = file.format
      return {
        ...base,
        filesizeBytes: toNumber(file.filesize),
        spectrumCount: toNumber(fmt.sourceTotalSpec),
        mzFormat: fmtLabel(MZ_FMT, fmt.sourceMzFmt),
        intensityFormat: fmtLabel(MZ_FMT, fmt.sourceIntenFmt),
        sourceCompression: fmtLabel(COMPRESSION, fmt.sourceCompression),
        accessions: fmt.toDict()
      }
    } finally {
      file.close()
    }
  } catch (err) {
    base.error = err instanceof Error ? err.message : String(err)
    return base
  }
}
