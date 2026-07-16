// Thin main-process wrapper around the native `mscompress` node-ts binding.
// This module is the ONLY place the addon is required, and it must never be
// imported from the renderer (it is externalized from the Vite bundle).
import { createRequire } from 'module'
import { statSync } from 'fs'
import { basename, dirname, extname, join } from 'path'
import { hrtime } from 'process'
import { COMPRESSION_PRESETS } from '../shared/ipc.ts'
import type {
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
  FileKind,
  FileSummary,
  LossyAlgo,
  MsLevelCount
} from '../shared/ipc.ts'

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

interface MsRuntimeArguments {
  threads: number
  zstdCompressionLevel: number
  targetMzFormat: number
  targetIntenFormat: number
  mzScaleFactor: number
  intScaleFactor: number
}

interface MsNativeExtractOptions {
  msLevel?: number
  indices?: number[]
  scanNumbers?: number[]
}

interface MsFile {
  path: string
  filesize: number | bigint
  format: MsDataFormat
  positions: MsPositions
  spectra: MsSpectra
  arguments: MsRuntimeArguments
  compress(outPath: string): MsFile
  decompress(outPath: string): MsFile
  extract(outPath: string, options: MsNativeExtractOptions): MsFile
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

// Lossy algorithm → target-format accession code (from src/mscompress.h). "none"
// keeps the default lossless ZSTD target.
const LOSSY_FORMAT: Record<LossyAlgo, number> = {
  none: 4700001, // _ZSTD_compression_ (lossless)
  cast: 4700002, // _cast_64_to_32_
  log: 4700003, // _log2_transform_
  delta16: 4700004, // _delta16_transform_
  delta32: 4700006, // _delta32_transform_
  vbr: 4700007 // _vbr_
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

// ---------------------------------------------------------------------------
// Convert operations: compress / decompress / extract
// ---------------------------------------------------------------------------

/** Strip the final extension from a base filename. */
function stripExt(name: string): string {
  const ext = extname(name)
  return ext ? name.slice(0, -ext.length) : name
}

/** Compute an output path in `outputDir` (or alongside the input). */
function outPathFor(inputPath: string, outputDir: string | undefined, newExt: string, suffix = ''): string {
  const dir = outputDir && outputDir.length > 0 ? outputDir : dirname(inputPath)
  const stem = stripExt(basename(inputPath))
  return join(dir, `${stem}${suffix}${newExt}`)
}

/** Build a ConvertResult from the finished output file. */
function makeResult(
  op: ConvertResult['op'],
  inputPath: string,
  outPath: string,
  startedAt: bigint
): ConvertResult {
  const inputBytes = getFilesize(inputPath)
  const outputBytes = statSync(outPath).size
  return {
    op,
    outPath,
    inputBytes,
    outputBytes,
    ratio: inputBytes > 0 ? outputBytes / inputBytes : 0,
    elapsedMs: Number(hrtime.bigint() - startedAt) / 1e6
  }
}

function errorResult(op: ConvertResult['op'], err: unknown): ConvertResult {
  return {
    op,
    outPath: '',
    inputBytes: 0,
    outputBytes: 0,
    ratio: 0,
    elapsedMs: 0,
    error: err instanceof Error ? err.message : String(err)
  }
}

/** Apply preset + advanced overrides onto a file's RuntimeArguments. */
function applyCompressArgs(file: MsFile, opts: CompressOptions): void {
  const preset = COMPRESSION_PRESETS[opts.preset] ?? COMPRESSION_PRESETS.default
  const mz = opts.mzLossy ?? preset.mzLossy
  const int = opts.intLossy ?? preset.intLossy
  file.arguments.threads = opts.threads ?? getNumThreads()
  file.arguments.zstdCompressionLevel = opts.zstdLevel ?? preset.zstdLevel
  file.arguments.targetMzFormat = LOSSY_FORMAT[mz]
  file.arguments.targetIntenFormat = LOSSY_FORMAT[int]
}

/** Compress an mzML → .msz. */
export function compress(path: string, opts: CompressOptions): ConvertResult {
  const startedAt = hrtime.bigint()
  let file: MsFile | null = null
  try {
    if (kindFromPath(path) !== 'mzML') {
      throw new Error('Only mzML files can be compressed.')
    }
    file = mod().read(path)
    applyCompressArgs(file, opts)
    const outPath = outPathFor(path, opts.outputDir, '.msz')
    const out = file.compress(outPath)
    out.close()
    return makeResult('compress', path, outPath, startedAt)
  } catch (err) {
    return errorResult('compress', err)
  } finally {
    file?.close()
  }
}

/** Decompress an .msz/.mszx → .mzML. */
export function decompress(path: string, opts: DecompressOptions): ConvertResult {
  const startedAt = hrtime.bigint()
  const kind = kindFromPath(path)
  let file: MsFile | null = null
  try {
    if (kind === 'mzML') {
      throw new Error('mzML files are already decompressed.')
    }
    file = openFile(path, kind)
    file.arguments.threads = opts.threads ?? getNumThreads()
    const outPath = outPathFor(path, opts.outputDir, '.mzML')
    const out = file.decompress(outPath)
    out.close()
    return makeResult('decompress', path, outPath, startedAt)
  } catch (err) {
    return errorResult('decompress', err)
  } finally {
    file?.close()
  }
}

/** Translate the UI extract options into the native filter object. */
function nativeExtractOptions(opts: ExtractOptions): MsNativeExtractOptions {
  if (opts.mode === 'mslevel') {
    return { msLevel: opts.msLevel ?? 2 }
  }
  if (opts.mode === 'scan') {
    const from = opts.fromScan ?? 0
    const to = Math.max(from, opts.toScan ?? from)
    const scanNumbers: number[] = []
    for (let s = from; s <= to; s++) scanNumbers.push(s)
    return { scanNumbers }
  }
  // index range
  const from = opts.fromIndex ?? 0
  const to = Math.max(from, opts.toIndex ?? from)
  const indices: number[] = []
  for (let i = from; i <= to; i++) indices.push(i)
  return { indices }
}

/** Extract a spectra subset → .mzML or .msz. */
export function extract(path: string, opts: ExtractOptions): ConvertResult {
  const startedAt = hrtime.bigint()
  const kind = kindFromPath(path)
  let file: MsFile | null = null
  try {
    file = openFile(path, kind)
    file.arguments.threads = opts.threads ?? getNumThreads()
    const ext = opts.outputFormat === 'msz' ? '.msz' : '.mzML'
    const outPath = outPathFor(path, opts.outputDir, ext, '.extracted')
    const out = file.extract(outPath, nativeExtractOptions(opts))
    out.close()
    return makeResult('extract', path, outPath, startedAt)
  } catch (err) {
    return errorResult('extract', err)
  } finally {
    file?.close()
  }
}
