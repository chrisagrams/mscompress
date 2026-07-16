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
  computeQC: 'qc:compute',
  readMszx: 'archive:read',
  settingsGet: 'settings:get',
  settingsSet: 'settings:set',
  // main -> renderer settings-change broadcast (webContents.send)
  settingsChange: 'settings:change',
  compress: 'convert:compress',
  decompress: 'convert:decompress',
  extract: 'convert:extract',
  // main -> renderer streamed progress (webContents.send)
  convertProgress: 'convert:progress',
  // MSTransfer batch queue
  queueAdd: 'queue:add',
  queueStart: 'queue:start',
  queuePause: 'queue:pause',
  queueClear: 'queue:clear',
  queueRemove: 'queue:remove',
  queueGetState: 'queue:getState',
  // main -> renderer queue-state broadcast (webContents.send)
  queueUpdate: 'queue:update'
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

// ---- Global settings -------------------------------------------------------

/** Persisted, app-wide settings (userData/settings.json). */
export interface AppSettings {
  /** Worker threads used for compress/decompress/extract. */
  threads: number
  /** Default local output directory for the Convert tab. */
  defaultOutputDir: string
  /** Default compression preset. */
  defaultPreset: Preset
  theme: 'dark' | 'light'
}

// ---- MSZX archive ----------------------------------------------------------

export interface MszxAnnotation {
  filename: string
  format: string
  compressed: boolean
  num_records: number | null
  description: string | null
}

/** Plain, serializable MSZX manifest for the Archive tab. */
export interface MszxManifest {
  path: string
  version: string
  created_at: string
  spectra_file: string
  num_spectra: number
  join_key: string
  source_file: string | null
  description: string | null
  annotations: MszxAnnotation[]
  /** Set instead of the manifest fields when the archive couldn't be read. */
  error?: string
}

// ---- MSTransfer batch queue ------------------------------------------------

export type QueueOp = 'compress' | 'decompress' | 'extract'
export type QueueStatus = 'queued' | 'running' | 'done' | 'error'

/** A remote destination for finished outputs. */
export interface RemoteDestination {
  id: string
  label: string
  type: 'local' | 'ssh' | 's3'
  /** local destinations are wired end-to-end; ssh/s3 are input-validating stubs. */
  configured: boolean
}

/** Stub destination list (a real settings store arrives in a later task). */
export const REMOTE_DESTINATIONS: RemoteDestination[] = [
  { id: 'local-archive', label: 'local-archive · MScompress-Archive', type: 'local', configured: true },
  { id: 'ssh-hpc', label: 'hpc · cgrams@aurora:/flare', type: 'ssh', configured: false },
  { id: 's3-lab', label: 'storage-a · s3://lab-archive', type: 's3', configured: false }
]

/** A single serializable queue job (settings are held privately in main). */
export interface QueueJob {
  id: string
  filePath: string
  fileName: string
  kind: FileKind
  op: QueueOp
  status: QueueStatus
  progress: number
  sizeBytes: number
  ratio?: number
  etaSec?: number
  /** local convert output path. */
  outPath?: string
  /** where the output finally landed after a remote transfer. */
  finalPath?: string
  destinationId?: string
  destinationLabel?: string
  error?: string
}

export interface QueueState {
  jobs: QueueJob[]
  running: number
  paused: boolean
  maxConcurrency: number
}

/** Request to enqueue a convert job (optionally routed to a remote dest). */
export interface QueueAddRequest {
  filePath: string
  fileName?: string
  kind: FileKind
  op: QueueOp
  settings: CompressOptions | DecompressOptions | ExtractOptions
  destinationId?: string
  remotePath?: string
  transferMode?: 'copy' | 'move'
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

// ---- QC dashboard ----------------------------------------------------------

export interface TicPoint {
  rt: number
  tic: number
}
export interface BpcPoint {
  rt: number
  bpc: number
}
export interface HeatCell {
  rt: number
  mz: number
  density: number
}
export interface QCHeatmap {
  rtBins: number
  mzBins: number
  mzMin: number
  mzMax: number
  rtMax: number
  cells: HeatCell[]
}
export interface PeaksBin {
  bin: string
  count: number
}

/**
 * QC dashboard data derived from a single pass over (a sample of) the file's
 * spectra. Same shapes the QCTab charts consume. `rt` values are in minutes
 * (or spectrum index when the file has no usable retention times).
 */
export interface QCData {
  path: string
  spectrumCount: number
  /** How many spectra were actually decoded (≤ spectrumCount when sampled). */
  sampledCount: number
  tic: TicPoint[]
  bpc: BpcPoint[]
  heatmap: QCHeatmap
  msLevelCounts: MsLevelCount[]
  peaksPerSpectrum: PeaksBin[]
  /** Set instead of the data fields when QC failed. */
  error?: string
}

export interface QCOptions {
  /** Max spectra to decode (stride-sampled beyond this). */
  maxSpectra?: number
  /** Max TIC/BPC points (RT-binned beyond this). */
  ticPoints?: number
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
  /** Compute QC dashboard data (TIC/BPC/heatmap/MS-levels/peaks) from spectra. */
  computeQC(path: string, opts?: QCOptions): Promise<QCData>
  /** Read an .mszx archive's manifest + annotations. */
  readMszx(path: string): Promise<MszxManifest>
  /** Current persisted settings. */
  getSettings(): Promise<AppSettings>
  /** Merge a partial update into settings, persist, and return the result. */
  setSettings(partial: Partial<AppSettings>): Promise<AppSettings>
  /** Subscribe to settings-change broadcasts. Returns an unsubscribe fn. */
  onSettingsChange(cb: (settings: AppSettings) => void): () => void
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
  /** Enqueue a convert job on the MSTransfer batch queue. Returns its id. */
  addQueueJob(req: QueueAddRequest): Promise<string>
  /** Start (unpause) the queue runner. */
  startQueue(): Promise<void>
  /** Pause the queue (running jobs finish; no new ones start). */
  pauseQueue(): Promise<void>
  /** Remove all non-running jobs. */
  clearQueue(): Promise<void>
  /** Remove a single non-running job by id. */
  removeJob(id: string): Promise<void>
  /** Current queue snapshot (for initial render). */
  getQueueState(): Promise<QueueState>
  /** Subscribe to queue-state broadcasts. Returns an unsubscribe fn. */
  onQueueUpdate(cb: (state: QueueState) => void): () => void
  /**
   * Resolve the absolute filesystem path of a dropped/selected File. Electron
   * removed `File.path`, so this proxies `webUtils.getPathForFile` from preload.
   * Synchronous (no IPC round-trip).
   */
  getPathForFile(file: File): string
}
