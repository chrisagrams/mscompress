// Shared IPC contract between the main process, preload bridge and renderer.
// Pure types + channel-name constants only — NO electron/node imports here, so
// it is safe to reference from the renderer (types are erased at build time).

/** Whitelisted IPC channels. The preload bridge only ever invokes these. */
export const IPC = {
  openFiles: 'dialog:openFiles',
  openOutputDir: 'dialog:openOutputDir',
  openExternal: 'shell:openExternal',
  revealInFolder: 'shell:revealInFolder',
  getDefaultOutputDir: 'app:getDefaultOutputDir',
  getVersion: 'sys:getVersion',
  getNumThreads: 'sys:getNumThreads',
  getFilesize: 'fs:getFilesize',
  analyze: 'file:analyze',
  compress: 'convert:compress',
  decompress: 'convert:decompress',
  extract: 'convert:extract',
  // main -> renderer streamed progress (webContents.send)
  convertProgress: 'convert:progress'
} as const

export type IpcChannel = (typeof IPC)[keyof typeof IPC]

/** Lossy pre-processing algorithms for m/z and intensity arrays. */
export type LossyAlgo = 'none' | 'cast' | 'log' | 'delta16' | 'delta32' | 'vbr'

/** Compression presets shown in the Convert UI. */
export type Preset = 'fastest' | 'fast' | 'default' | 'better'

/** How an extract selects its spectra subset. */
export type ExtractMode = 'mslevel' | 'scan' | 'index'

/**
 * Preset → concrete settings. The Advanced accordion overrides these per-field.
 * Pure data so the renderer can seed its form controls from the same source the
 * main process uses to build RuntimeArguments.
 */
export const COMPRESSION_PRESETS: Record<
  Preset,
  { zstdLevel: number; mzLossy: LossyAlgo; intLossy: LossyAlgo }
> = {
  fastest: { zstdLevel: 1, mzLossy: 'none', intLossy: 'none' },
  fast: { zstdLevel: 3, mzLossy: 'none', intLossy: 'none' },
  default: { zstdLevel: 9, mzLossy: 'none', intLossy: 'none' },
  better: { zstdLevel: 19, mzLossy: 'none', intLossy: 'none' }
}

export interface CompressOptions {
  preset: Preset
  /** Advanced overrides (undefined → use the preset default). */
  mzLossy?: LossyAlgo
  intLossy?: LossyAlgo
  zstdLevel?: number
  threads?: number
  outputDir?: string
}

export interface DecompressOptions {
  threads?: number
  outputDir?: string
}

export interface ExtractOptions {
  mode: ExtractMode
  msLevel?: number
  fromScan?: number
  toScan?: number
  fromIndex?: number
  toIndex?: number
  outputFormat: 'mzML' | 'msz'
  threads?: number
  outputDir?: string
}

/** Plain result of a convert operation (error reported structurally). */
export interface ConvertResult {
  op: 'compress' | 'decompress' | 'extract'
  outPath: string
  inputBytes: number
  outputBytes: number
  /** outputBytes / inputBytes (0 on error). */
  ratio: number
  elapsedMs: number
  error?: string
}

/** Streamed progress event (main → renderer). */
export interface ConvertProgress {
  id: string
  op: 'compress' | 'decompress' | 'extract'
  phase: 'start' | 'step' | 'done' | 'error'
  file: string
  message?: string
  result?: ConvertResult
  error?: string
}

/** Recognized source file kinds. */
export type FileKind = 'mzML' | 'msz' | 'mszx'

/**
 * Plain, JSON-serializable summary of a file produced by the main process via
 * the native `mscompress` binding. All numeric fields are plain numbers (any
 * native BigInt is coerced) so it crosses the IPC boundary cleanly.
 */
export interface MsLevelCount {
  /** "MS1" | "MS2" | "MSn" */
  level: string
  count: number
}

export interface FileSummary {
  path: string
  fileName: string
  kind: FileKind
  filesizeBytes: number
  /** null when not available (e.g. on parse error). */
  spectrumCount: number | null
  mzFormat: string | null
  intensityFormat: string | null
  sourceCompression: string | null
  /** Per-MS-level spectrum counts, ordered MS1, MS2, MSn (empty on error). */
  msLevelCounts: MsLevelCount[]
  /** Retention-time span in seconds, [min, max]. [0, 0] when unavailable. */
  rtRangeSec: [number, number]
  /** PSI-MS accession dict from DataFormat.toDict(), when available. */
  accessions: Record<string, string | number> | null
  /** Populated instead of the format fields when analysis failed. */
  error?: string
}

/** The typed surface exposed on `window.api` in the renderer. */
export interface Api {
  /** Open a multi-select file dialog (mzML/msz/mszx). Returns chosen paths. */
  openFiles(): Promise<string[]>
  /** Open a directory picker. Returns the chosen dir, or null if cancelled. */
  openOutputDir(): Promise<string | null>
  /** Open a URL in the user's default browser. */
  openExternal(url: string): Promise<void>
  /** Reveal a file in the OS file manager (selects it). */
  revealInFolder(path: string): Promise<void>
  /** Default output directory (the user's Downloads folder). */
  getDefaultOutputDir(): Promise<string>
  /** Native mscompress backend version string. */
  getVersion(): Promise<string>
  /** Number of worker threads the backend will use. */
  getNumThreads(): Promise<number>
  /** File size in bytes. */
  getFilesize(path: string): Promise<number>
  /** Analyze a file via the native binding and return a serializable summary. */
  analyze(path: string): Promise<FileSummary>
  /** Compress an mzML file to .msz. */
  compress(path: string, opts: CompressOptions): Promise<ConvertResult>
  /** Decompress an .msz/.mszx file to .mzML. */
  decompress(path: string, opts: DecompressOptions): Promise<ConvertResult>
  /** Extract a spectra subset to a new .mzML/.msz. */
  extract(path: string, opts: ExtractOptions): Promise<ConvertResult>
  /**
   * Subscribe to streamed convert progress events. Returns an unsubscribe fn.
   */
  onConvertProgress(cb: (p: ConvertProgress) => void): () => void
  /**
   * Resolve the absolute filesystem path of a dropped/selected File. Electron
   * removed `File.path`, so this proxies `webUtils.getPathForFile` from preload.
   * Synchronous (no IPC round-trip).
   */
  getPathForFile(file: File): string
}
