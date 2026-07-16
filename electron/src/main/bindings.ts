// Thin main-process wrapper around the native `mscompress` node-ts binding.
// This module is the ONLY place the addon is required, and it must never be
// imported from the renderer (it is externalized from the Vite bundle).
import { createRequire } from 'module'
import { basename, extname } from 'path'
import type { FileKind, FileSummary, MsLevelCount } from '../shared/ipc'

// The binding ships as ESM; require(ESM) (Node 22.12+/Electron 38) loads its
// namespace synchronously, which is all we need for these cheap header reads.
const require = createRequire(import.meta.url)

// The binding's public surface we use here. Typed loosely to avoid pulling the
// package's ESM type graph into the CJS main bundle; the shapes are stable.
interface MsDataFormat {
  sourceMzFmt: number
  sourceIntenFmt: number
  sourceCompression: number
  sourceTotalSpec: number
  toDict(): Record<string, string | number>
}

interface MsPositions {
  msLevels: Uint16Array
  retTimes: Float32Array | null
}

interface MsSpectrum {
  readonly peaks: Float64Array
}

interface MsSpectra {
  readonly length: number
  get(index: number): MsSpectrum
}

interface MsFile {
  path: string
  filesize: number | bigint
  format: MsDataFormat
  positions: MsPositions
  spectra: MsSpectra
  close(): void
}

interface MscompressModule {
  getVersion(): string
  getNumThreads(): number
  getFilesize(filePath: string): number
  read(filePath: string): MsFile
  MSZXFile: { open(filePath: string): MsFile }
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

// Cap the peak-decode integrity probe so huge files stay responsive; corruption
// is sampled across the whole file rather than only the first few spectra.
const INTEGRITY_SAMPLE_CAP = 128

function fmtLabel(map: Record<number, string>, code: number): string | null {
  return map[code] ?? (code ? `code:${code}` : null)
}

function kindFromPath(path: string): FileKind {
  const ext = extname(path).toLowerCase()
  if (ext === '.msz') return 'msz'
  if (ext === '.mszx') return 'mszx'
  return 'mzML'
}

/** Group per-spectrum MS levels into ordered MS1/MS2/MSn counts. */
function msLevelCounts(levels: Uint16Array): MsLevelCount[] {
  let ms1 = 0
  let ms2 = 0
  let msn = 0
  for (const lvl of levels) {
    if (lvl >= 3) msn++
    else if (lvl === 2) ms2++
    else ms1++
  }
  const out: MsLevelCount[] = []
  if (ms1) out.push({ level: 'MS1', count: ms1 })
  if (ms2) out.push({ level: 'MS2', count: ms2 })
  if (msn) out.push({ level: 'MSn', count: msn })
  return out
}

/** Min/max of the retention-time array (seconds), ignoring NaN. [0,0] if none. */
function rtRange(retTimes: Float32Array | null): [number, number] {
  if (!retTimes || retTimes.length === 0) return [0, 0]
  let min = Infinity
  let max = -Infinity
  for (const v of retTimes) {
    if (Number.isFinite(v)) {
      if (v < min) min = v
      if (v > max) max = v
    }
  }
  if (!Number.isFinite(min)) return [0, 0]
  return [min, max]
}

/**
 * Decode peaks for a bounded sample of spectra. Accessing `.peaks` forces the
 * native base64/zlib decode and validates m/z ↔ intensity length parity, so a
 * corrupt file throws here even though its header parses cleanly. Bounded and
 * stride-sampled so very large files stay responsive.
 */
function probeIntegrity(file: MsFile): void {
  const length = file.spectra.length
  if (length === 0) return
  const stride = Math.max(1, Math.floor(length / INTEGRITY_SAMPLE_CAP))
  for (let i = 0; i < length; i += stride) {
    void file.spectra.get(i).peaks
  }
}

/** Open a file by kind: mszx uses MSZXFile.open (read() can't detect the tar). */
function openFile(path: string, kind: FileKind): MsFile {
  return kind === 'mszx' ? mod().MSZXFile.open(path) : mod().read(path)
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
 * Analyze a file and return a rich, plain JSON-serializable summary for the
 * Inspector. Opens the file via the native binding, reads the header DataFormat
 * plus the pre-scanned MS-level / retention-time arrays, then runs a bounded
 * peak-decode integrity probe. Always resolves — read errors and data
 * corruption are reported in the `error` field rather than thrown.
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
    msLevelCounts: [],
    rtRangeSec: [0, 0],
    accessions: null
  }

  let file: MsFile | null = null
  try {
    file = openFile(path, kind)
    const fmt = file.format
    const summary: FileSummary = {
      ...base,
      filesizeBytes: toNumber(file.filesize),
      spectrumCount: file.spectra.length,
      mzFormat: fmtLabel(MZ_FMT, fmt.sourceMzFmt),
      intensityFormat: fmtLabel(MZ_FMT, fmt.sourceIntenFmt),
      sourceCompression: fmtLabel(COMPRESSION, fmt.sourceCompression),
      msLevelCounts: msLevelCounts(file.positions.msLevels),
      rtRangeSec: rtRange(file.positions.retTimes),
      accessions: fmt.toDict()
    }
    // Surfaces corrupt peak data that header parsing alone would miss.
    probeIntegrity(file)
    return summary
  } catch (err) {
    // Still report the size if we can get it cheaply, so the row isn't blank.
    let filesizeBytes = 0
    try {
      filesizeBytes = getFilesize(path)
    } catch {
      /* ignore */
    }
    return {
      ...base,
      filesizeBytes,
      error: err instanceof Error ? err.message : String(err)
    }
  } finally {
    file?.close()
  }
}
