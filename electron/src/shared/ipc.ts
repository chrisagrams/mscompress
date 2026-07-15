// Shared IPC contract between the main process, preload bridge and renderer.
// Pure types + channel-name constants only — NO electron/node imports here, so
// it is safe to reference from the renderer (types are erased at build time).

/** Whitelisted IPC channels. The preload bridge only ever invokes these. */
export const IPC = {
  openFiles: 'dialog:openFiles',
  openOutputDir: 'dialog:openOutputDir',
  openExternal: 'shell:openExternal',
  getDefaultOutputDir: 'app:getDefaultOutputDir',
  getVersion: 'sys:getVersion',
  getNumThreads: 'sys:getNumThreads',
  getFilesize: 'fs:getFilesize',
  analyze: 'file:analyze'
} as const

export type IpcChannel = (typeof IPC)[keyof typeof IPC]

/** Recognized source file kinds. */
export type FileKind = 'mzML' | 'msz' | 'mszx'

/**
 * Plain, JSON-serializable summary of a file produced by the main process via
 * the native `mscompress` binding. All numeric fields are plain numbers (any
 * native BigInt is coerced) so it crosses the IPC boundary cleanly.
 */
export interface FileSummary {
  path: string
  fileName: string
  kind: FileKind
  filesizeBytes: number
  /** null when not cheaply available (e.g. mszx archives, or on parse error). */
  spectrumCount: number | null
  mzFormat: string | null
  intensityFormat: string | null
  sourceCompression: string | null
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
  /**
   * Resolve the absolute filesystem path of a dropped/selected File. Electron
   * removed `File.path`, so this proxies `webUtils.getPathForFile` from preload.
   * Synchronous (no IPC round-trip).
   */
  getPathForFile(file: File): string
}
