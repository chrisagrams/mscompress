// Thin main-process wrapper around the native `mscompress` node-ts binding.
// This module is the ONLY place the addon is required, and it must never be
// imported from the renderer (it is externalized from the Vite bundle).
import { createRequire } from "module"
import { existsSync, statSync } from "fs"
import { basename, dirname, extname, join } from "path"
import { hrtime } from "process"
import { Worker } from "worker_threads"
import { COMPRESSION_PRESETS } from "../shared/ipc.ts"
import type {
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
  FileKind,
  FileSummary,
  MsLevelCount,
  MszxManifest,
  LossyAlgo,
  QCData,
  QCOptions,
} from "../shared/ipc.ts"

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
  readonly mz: Float64Array | Float32Array
  readonly intensity: Float64Array | Float32Array
  readonly size: number
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

interface MsManifestData {
  version: string
  created_at: string
  spectra_file: string
  num_spectra: number | bigint
  join_key: string
  source_file?: string
  description?: string
  annotations: {
    filename: string
    format: string
    compressed: boolean
    num_records?: number | bigint
    description?: string
  }[]
}

interface MsMszxFile extends MsFile {
  manifest: { toJSON(): MsManifestData }
}

interface MscompressModule {
  getVersion(): string
  getNumThreads(): number
  getFilesize(filePath: string): number
  read(filePath: string): MsFile
  MSZXFile: { open(filePath: string): MsMszxFile }
}

let cached: MscompressModule | null = null

/**
 * Lazily load the native addon (deferred so the app can start even if the
 * binding is temporarily unavailable, and to keep startup cheap). In a packaged
 * app the `mscompress` package (node-ts dist + build/Release/*.node) is shipped
 * under `resources/node-ts` via electron-builder `extraResources`; resolve it
 * from there first. In dev / the Node smoke test `process.resourcesPath` either
 * points elsewhere or is undefined, so we fall back to normal resolution of the
 * `mscompress` node_modules symlink.
 */
function loadModule(): MscompressModule {
  const resourced = process.resourcesPath
    ? join(process.resourcesPath, "node-ts", "dist", "index.js")
    : ""
  if (resourced && existsSync(resourced)) {
    return require(resourced) as MscompressModule
  }
  return require("mscompress") as MscompressModule
}

function mod(): MscompressModule {
  if (!cached) {
    cached = loadModule()
  }
  return cached
}

/** Coerce a possibly-BigInt native size to a plain, IPC-safe number. */
function toNumber(v: number | bigint): number {
  return typeof v === "bigint" ? Number(v) : v
}

// PSI-MS accession codes → human-readable labels (mirrors the C core).
const MZ_FMT: Record<number, string> = {
  1000519: "int32",
  1000520: "float16",
  1000521: "float32",
  1000522: "int64",
  1000523: "float64",
}
const COMPRESSION: Record<number, string> = {
  1000574: "zlib",
  1000576: "none",
  4700001: "zstd",
}

// Lossy algorithm → target-format accession code (from src/mscompress.h). "none"
// keeps the default lossless ZSTD target.
const LOSSY_FORMAT: Record<LossyAlgo, number> = {
  none: 4700001, // _ZSTD_compression_ (lossless)
  cast: 4700002, // _cast_64_to_32_
  log: 4700003, // _log2_transform_
  delta16: 4700004, // _delta16_transform_
  delta32: 4700006, // _delta32_transform_
  vbr: 4700007, // _vbr_
}

// Cap the peak-decode integrity probe so huge files stay responsive; corruption
// is sampled across the whole file rather than only the first few spectra.
const INTEGRITY_SAMPLE_CAP = 128

function fmtLabel(map: Record<number, string>, code: number): string | null {
  return map[code] ?? (code ? `code:${code}` : null)
}

function kindFromPath(path: string): FileKind {
  const ext = extname(path).toLowerCase()
  if (ext === ".msz") return "msz"
  if (ext === ".mszx") return "mszx"
  return "mzML"
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
  if (ms1) out.push({ level: "MS1", count: ms1 })
  if (ms2) out.push({ level: "MS2", count: ms2 })
  if (msn) out.push({ level: "MSn", count: msn })
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
  return kind === "mszx" ? mod().MSZXFile.open(path) : mod().read(path)
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
    accessions: null,
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
      accessions: fmt.toDict(),
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
      error: err instanceof Error ? err.message : String(err),
    }
  } finally {
    file?.close()
  }
}

// ---------------------------------------------------------------------------
// MSZX archive
// ---------------------------------------------------------------------------

/**
 * Read an .mszx archive's manifest + annotations into a plain, serializable
 * object. Always resolves — a bad/missing archive is reported via `error`.
 */
export function readMszx(path: string): MszxManifest {
  const base: MszxManifest = {
    path,
    version: "",
    created_at: "",
    spectra_file: "",
    num_spectra: 0,
    join_key: "",
    source_file: null,
    description: null,
    annotations: [],
  }

  let file: MsMszxFile | null = null
  try {
    file = mod().MSZXFile.open(path)
    const m = file.manifest.toJSON()
    return {
      path,
      version: m.version,
      created_at: m.created_at,
      spectra_file: m.spectra_file,
      num_spectra: toNumber(m.num_spectra),
      join_key: m.join_key,
      source_file: m.source_file ?? null,
      description: m.description ?? null,
      annotations: (m.annotations ?? []).map((a) => ({
        filename: a.filename,
        format: a.format,
        compressed: Boolean(a.compressed),
        num_records: a.num_records != null ? toNumber(a.num_records) : null,
        description: a.description ?? null,
      })),
    }
  } catch (err) {
    return { ...base, error: err instanceof Error ? err.message : String(err) }
  } finally {
    file?.close()
  }
}

// ---------------------------------------------------------------------------
// QC dashboard
// ---------------------------------------------------------------------------

const QC_DEFAULT_MAX_SPECTRA = 4000
const QC_DEFAULT_TIC_POINTS = 300
const QC_RT_BINS = 300
const QC_MZ_BINS = 200
const PEAK_BINS: { label: string; max: number }[] = [
  { label: "0-100", max: 100 },
  { label: "100-250", max: 250 },
  { label: "250-500", max: 500 },
  { label: "500-1k", max: 1000 },
  { label: "1k-2k", max: 2000 },
  { label: "2k-4k", max: 4000 },
  { label: "4k+", max: Infinity },
]

function peakBinLabel(size: number): string {
  for (const b of PEAK_BINS) if (size < b.max) return b.label
  return PEAK_BINS[PEAK_BINS.length - 1].label
}

function clampBin(v: number, bins: number): number {
  if (v < 0) return 0
  if (v > bins - 1) return bins - 1
  return v
}

/**
 * Compute QC dashboard data in a single decode pass over a sample of the
 * spectra. Heavy work stays in the main process; payloads are bounded (TIC/BPC
 * capped by RT-binning, heatmap is a fixed grid). Always resolves — failures
 * are reported in `error`.
 */
export function computeQC(path: string, opts: QCOptions = {}): QCData {
  const kind = kindFromPath(path)
  const maxSpectra = opts.maxSpectra ?? QC_DEFAULT_MAX_SPECTRA
  const ticPoints = opts.ticPoints ?? QC_DEFAULT_TIC_POINTS
  const emptyHeatmap = {
    rtBins: QC_RT_BINS,
    mzBins: QC_MZ_BINS,
    mzMin: 0,
    mzMax: 0,
    rtMax: 0,
    cells: [],
  }
  const base: QCData = {
    path,
    spectrumCount: 0,
    sampledCount: 0,
    tic: [],
    bpc: [],
    heatmap: emptyHeatmap,
    msLevelCounts: [],
    peaksPerSpectrum: [],
  }

  let file: MsFile | null = null
  try {
    file = openFile(path, kind)
    const length = file.spectra.length
    const msLevels = file.positions.msLevels
    const retTimes = file.positions.retTimes

    // MS-level counts come from the (full, cheap) pre-scanned array.
    const levelCounts = msLevelCounts(msLevels)

    // Retention-time scale (minutes). Fall back to spectrum index if the file
    // carries no usable retention times (e.g. some msz).
    let rtMaxSec = 0
    if (retTimes) {
      for (const v of retTimes) if (Number.isFinite(v) && v > rtMaxSec) rtMaxSec = v
    }
    const useIndexRt = !(rtMaxSec > 0)
    const rtMax = useIndexRt ? Math.max(1, length - 1) : rtMaxSec / 60
    const rtOf = (i: number): number =>
      useIndexRt ? i : retTimes && Number.isFinite(retTimes[i]) ? retTimes[i] / 60 : 0

    // Sample stride so we decode at most `maxSpectra` spectra.
    const stride = Math.max(1, Math.ceil(length / maxSpectra))

    const ticRaw: TicPointLocal[] = []
    const peakCounts = new Map<string, number>()
    // Heatmap accumulator: key = rtBin * 1e6 + mzInt → summed intensity.
    const heatAccum = new Map<number, number>()
    let mzMinInt = Infinity
    let mzMaxInt = -Infinity
    let sampledCount = 0

    for (let i = 0; i < length; i += stride) {
      const spec = file.spectra.get(i)
      const mz = spec.mz
      const inten = spec.intensity
      const n = Math.min(mz.length, inten.length)
      sampledCount++

      // TIC (sum) + BPC (max) for this spectrum.
      let sum = 0
      let max = 0
      for (let k = 0; k < n; k++) {
        const y = inten[k]
        sum += y
        if (y > max) max = y
      }
      const rt = rtOf(i)
      ticRaw.push({ rt, tic: sum, bpc: max })

      // Peaks-per-spectrum histogram.
      const label = peakBinLabel(mz.length)
      peakCounts.set(label, (peakCounts.get(label) ?? 0) + 1)

      // Heatmap 2D histogram of intensity over (RT, m/z).
      const rtBin = clampBin(Math.round((rt / rtMax) * (QC_RT_BINS - 1)), QC_RT_BINS)
      for (let k = 0; k < n; k++) {
        const mzInt = Math.round(mz[k])
        if (mzInt < mzMinInt) mzMinInt = mzInt
        if (mzInt > mzMaxInt) mzMaxInt = mzInt
        const key = rtBin * 1_000_000 + mzInt
        heatAccum.set(key, (heatAccum.get(key) ?? 0) + inten[k])
      }
    }

    // TIC/BPC: one point per sampled spectrum, RT-binned if there are too many.
    ticRaw.sort((a, b) => a.rt - b.rt)
    const { tic, bpc } = foldTic(ticRaw, ticPoints, rtMax)

    // Heatmap: remap integer-m/z accumulation into the fixed mz-bin grid.
    const mzMin = Number.isFinite(mzMinInt) ? mzMinInt : 0
    const mzMax = Number.isFinite(mzMaxInt) && mzMaxInt > mzMin ? mzMaxInt : mzMin + 1
    const grid: number[][] = Array.from({ length: QC_RT_BINS }, () =>
      new Array<number>(QC_MZ_BINS).fill(0),
    )
    for (const [key, val] of heatAccum) {
      const rtBin = Math.floor(key / 1_000_000)
      const mzInt = key % 1_000_000
      const mzBin = clampBin(
        Math.round(((mzInt - mzMin) / (mzMax - mzMin)) * (QC_MZ_BINS - 1)),
        QC_MZ_BINS,
      )
      grid[rtBin][mzBin] += val
    }
    // Emit dense cells at exact bin anchors (log-scaled density for readability),
    // so the renderer's inverse mapping reconstructs the grid precisely.
    const cells = []
    for (let r = 0; r < QC_RT_BINS; r++) {
      const rtVal = (r / (QC_RT_BINS - 1)) * rtMax
      for (let m = 0; m < QC_MZ_BINS; m++) {
        const v = grid[r][m]
        if (v <= 0) continue
        const mzVal = mzMin + (m / (QC_MZ_BINS - 1)) * (mzMax - mzMin)
        cells.push({ rt: rtVal, mz: mzVal, density: Math.log1p(v) })
      }
    }

    const peaksPerSpectrum = PEAK_BINS.map((b) => ({
      bin: b.label,
      count: peakCounts.get(b.label) ?? 0,
    }))

    return {
      path,
      spectrumCount: length,
      sampledCount,
      tic,
      bpc,
      heatmap: { rtBins: QC_RT_BINS, mzBins: QC_MZ_BINS, mzMin, mzMax, rtMax, cells },
      msLevelCounts: levelCounts,
      peaksPerSpectrum,
    }
  } catch (err) {
    return { ...base, error: err instanceof Error ? err.message : String(err) }
  } finally {
    file?.close()
  }
}

interface TicPointLocal {
  rt: number
  tic: number
  bpc: number
}

/** Reduce per-spectrum TIC/BPC points to at most `ticPoints`, RT-binning. */
function foldTic(
  raw: TicPointLocal[],
  ticPoints: number,
  rtMax: number,
): { tic: { rt: number; tic: number }[]; bpc: { rt: number; bpc: number }[] } {
  if (raw.length <= ticPoints) {
    return {
      tic: raw.map((p) => ({ rt: +p.rt.toFixed(4), tic: p.tic })),
      bpc: raw.map((p) => ({ rt: +p.rt.toFixed(4), bpc: p.bpc })),
    }
  }
  const sumBin = new Array<number>(ticPoints).fill(0)
  const maxBin = new Array<number>(ticPoints).fill(0)
  const seen = new Array<boolean>(ticPoints).fill(false)
  const span = rtMax > 0 ? rtMax : 1
  for (const p of raw) {
    const b = clampBin(Math.floor((p.rt / span) * ticPoints), ticPoints)
    sumBin[b] += p.tic
    if (p.bpc > maxBin[b]) maxBin[b] = p.bpc
    seen[b] = true
  }
  const tic: { rt: number; tic: number }[] = []
  const bpc: { rt: number; bpc: number }[] = []
  for (let b = 0; b < ticPoints; b++) {
    if (!seen[b]) continue
    const rt = +(((b + 0.5) / ticPoints) * span).toFixed(4)
    tic.push({ rt, tic: sumBin[b] })
    bpc.push({ rt, bpc: maxBin[b] })
  }
  return { tic, bpc }
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
function outPathFor(
  inputPath: string,
  outputDir: string | undefined,
  newExt: string,
  suffix = "",
): string {
  const dir = outputDir && outputDir.length > 0 ? outputDir : dirname(inputPath)
  const stem = stripExt(basename(inputPath))
  return join(dir, `${stem}${suffix}${newExt}`)
}

/** Build a ConvertResult from the finished output file. */
function makeResult(
  op: ConvertResult["op"],
  inputPath: string,
  outPath: string,
  startedAt: bigint,
): ConvertResult {
  const inputBytes = getFilesize(inputPath)
  const outputBytes = statSync(outPath).size
  return {
    op,
    outPath,
    inputBytes,
    outputBytes,
    ratio: inputBytes > 0 ? outputBytes / inputBytes : 0,
    elapsedMs: Number(hrtime.bigint() - startedAt) / 1e6,
  }
}

function errorResult(op: ConvertResult["op"], err: unknown): ConvertResult {
  return {
    op,
    outPath: "",
    inputBytes: 0,
    outputBytes: 0,
    ratio: 0,
    elapsedMs: 0,
    error: err instanceof Error ? err.message : String(err),
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
    if (kindFromPath(path) !== "mzML") {
      throw new Error("Only mzML files can be compressed.")
    }
    file = mod().read(path)
    applyCompressArgs(file, opts)
    const outPath = outPathFor(path, opts.outputDir, ".msz")
    const out = file.compress(outPath)
    out.close()
    return makeResult("compress", path, outPath, startedAt)
  } catch (err) {
    return errorResult("compress", err)
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
    if (kind === "mzML") {
      throw new Error("mzML files are already decompressed.")
    }
    file = openFile(path, kind)
    file.arguments.threads = opts.threads ?? getNumThreads()
    const outPath = outPathFor(path, opts.outputDir, ".mzML")
    const out = file.decompress(outPath)
    out.close()
    return makeResult("decompress", path, outPath, startedAt)
  } catch (err) {
    return errorResult("decompress", err)
  } finally {
    file?.close()
  }
}

/** Translate the UI extract options into the native filter object. */
function nativeExtractOptions(opts: ExtractOptions): MsNativeExtractOptions {
  if (opts.mode === "mslevel") {
    return { msLevel: opts.msLevel ?? 2 }
  }
  if (opts.mode === "scan") {
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
    const ext = opts.outputFormat === "msz" ? ".msz" : ".mzML"
    const outPath = outPathFor(path, opts.outputDir, ext, ".extracted")
    const out = file.extract(outPath, nativeExtractOptions(opts))
    out.close()
    return makeResult("extract", path, outPath, startedAt)
  } catch (err) {
    return errorResult("extract", err)
  } finally {
    file?.close()
  }
}

// ---------------------------------------------------------------------------
// Async offload: run the (synchronous) native convert in a worker thread so it
// never blocks the Electron main JS thread. The sync compress/decompress/extract
// above stay intact (the smoke test drives them directly); this is an additive
// path. One-shot worker per job — a pool is overkill at maxConcurrency 1.
// ---------------------------------------------------------------------------

/**
 * Resolve the built worker chunk next to this module. When bundled by
 * electron-vite this file's URL ends in `.js` and `convert-worker.js` sits
 * beside it in `out/main/`; when run directly under Node's TS type-stripping
 * (smoke/probe scripts) it ends in `.ts` and `convert-worker.ts` sits beside it.
 */
function workerUrl(): URL {
  const name = import.meta.url.endsWith(".ts") ? "./convert-worker.ts" : "./convert-worker.js"
  return new URL(name, import.meta.url)
}

/**
 * Run a convert off the main thread. Always resolves with a ConvertResult —
 * worker/spawn failures are mapped to an `errorResult`-shaped value (never
 * thrown) so callers keep the same contract as the sync path.
 */
export function runConvertInWorker(
  op: "compress" | "decompress" | "extract",
  path: string,
  opts: CompressOptions | DecompressOptions | ExtractOptions,
): Promise<ConvertResult> {
  return new Promise((resolve) => {
    let settled = false
    let worker: Worker
    const finish = (result: ConvertResult): void => {
      if (settled) return
      settled = true
      void worker.terminate()
      resolve(result)
    }
    try {
      worker = new Worker(workerUrl())
    } catch (err) {
      resolve(errorResult(op, err))
      return
    }
    worker.once("message", (result: ConvertResult) => finish(result))
    worker.once("error", (err) => finish(errorResult(op, err)))
    worker.once("exit", (code) => {
      if (code !== 0) finish(errorResult(op, new Error(`Convert worker exited with code ${code}`)))
    })
    worker.postMessage({ op, path, opts })
  })
}
