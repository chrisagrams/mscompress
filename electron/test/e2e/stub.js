// Deterministic `window.api` stub injected into the page before any renderer
// script runs. Shapes mirror src/shared/ipc.ts (FileSummary, QCData, QueueState,
// MszxManifest, AppSettings, ConvertResult). Fixture values are modeled on the
// real repo test data in ../test/data (test.mzML has 50 spectra, etc.) but the
// files are never actually read — this exercises UI wiring, not the C library.

const mzML = "/fixtures/test.mzML"
const msz = "/fixtures/test.msz"
const mszx = "/fixtures/test.mszx"
const mzML2 = "/fixtures/run2.mzML"
const mszxBatch = "/fixtures/cohort.mszx"

export const fixtures = {
  version: "1.2.3-test",
  numThreads: 16,
  defaultOutputDir: "/home/lab/Downloads",
  settings: {
    threads: 8,
    defaultOutputDir: "/home/lab/Downloads",
    defaultPreset: "default",
    theme: "dark",
  },
  openFilesResult: [mzML, msz, mszx, mzML2, mszxBatch],
  files: {
    [mzML]: {
      path: mzML,
      fileName: "test.mzML",
      kind: "mzML",
      filesizeBytes: 4_946_404,
      spectrumCount: 50,
      mzFormat: "float64",
      intensityFormat: "float32",
      sourceCompression: "zlib",
      msLevelCounts: [
        { level: "MS1", count: 10 },
        { level: "MS2", count: 40 },
      ],
      rtRangeSec: [0.5, 1800.2],
      accessions: null,
    },
    [msz]: {
      path: msz,
      fileName: "test.msz",
      kind: "msz",
      filesizeBytes: 952_047,
      spectrumCount: 50,
      mzFormat: "float64",
      intensityFormat: "float32",
      sourceCompression: "zstd",
      msLevelCounts: [
        { level: "MS1", count: 10 },
        { level: "MS2", count: 40 },
      ],
      rtRangeSec: [0.5, 1800.2],
      accessions: null,
    },
    [mszx]: {
      path: mszx,
      fileName: "test.mszx",
      kind: "mszx",
      filesizeBytes: 931_811,
      spectrumCount: 50,
      mzFormat: "float64",
      intensityFormat: "float32",
      sourceCompression: "zstd",
      msLevelCounts: [
        { level: "MS1", count: 10 },
        { level: "MS2", count: 40 },
      ],
      rtRangeSec: [0.5, 1800.2],
      accessions: null,
    },
    [mzML2]: {
      path: mzML2,
      fileName: "run2.mzML",
      kind: "mzML",
      filesizeBytes: 909_920,
      spectrumCount: 100,
      mzFormat: "float64",
      intensityFormat: "float32",
      sourceCompression: "zlib",
      msLevelCounts: [
        { level: "MS1", count: 20 },
        { level: "MS2", count: 80 },
      ],
      rtRangeSec: [0.5, 1400.0],
      accessions: null,
    },
    [mszxBatch]: {
      path: mszxBatch,
      fileName: "cohort.mszx",
      kind: "mszx",
      filesizeBytes: 1_841_731,
      spectrumCount: 150,
      mzFormat: "float64",
      intensityFormat: "float32",
      sourceCompression: "zstd",
      msLevelCounts: [
        { level: "MS1", count: 30 },
        { level: "MS2", count: 120 },
      ],
      rtRangeSec: [0.5, 1800.2],
      accessions: null,
    },
  },
  qc: {
    spectrumCount: 50,
    sampledCount: 50,
    tic: [
      { rt: 0.1, tic: 2_000_000 },
      { rt: 0.4, tic: 5_400_000 },
      { rt: 0.7, tic: 8_100_000 },
      { rt: 1.0, tic: 4_300_000 },
      { rt: 1.3, tic: 2_600_000 },
    ],
    bpc: [
      { rt: 0.1, bpc: 900_000 },
      { rt: 0.4, bpc: 2_100_000 },
      { rt: 0.7, bpc: 3_300_000 },
      { rt: 1.0, bpc: 1_600_000 },
      { rt: 1.3, bpc: 1_050_000 },
    ],
    heatmap: {
      rtBins: 6,
      mzBins: 4,
      mzMin: 300,
      mzMax: 1500,
      rtMax: 1.3,
      cells: (() => {
        const cells = []
        for (let r = 0; r < 6; r++) {
          for (let m = 0; m < 4; m++) {
            cells.push({
              rt: (r / 6) * 1.3,
              mz: 300 + (m / 4) * 1200,
              density: ((r + m) % 5) / 4,
            })
          }
        }
        return cells
      })(),
    },
    msLevelCounts: [
      { level: "MS1", count: 10 },
      { level: "MS2", count: 40 },
    ],
    peaksPerSpectrum: [
      { bin: "0-100", count: 6 },
      { bin: "100-250", count: 18 },
      { bin: "250-500", count: 20 },
      { bin: "500+", count: 6 },
    ],
  },
  manifest: {
    container: "single",
    version: "1.0",
    created_at: "2026-05-12T14:03:22Z",
    spectra_file: "spectra.msz",
    num_spectra: 50,
    join_key: "scan_number",
    source_file: "test.mzML",
    description: "HEK293 tryptic digest, Orbitrap, DDA",
    annotations: [
      {
        filename: "psms.percolator.tsv",
        format: "percolator_tsv",
        compressed: true,
        num_records: 128,
        description: "Percolator PSMs (q<0.01)",
      },
      {
        filename: "peptides.pep.xml",
        format: "pepxml",
        compressed: false,
        num_records: 96,
        description: "Comet search results",
      },
    ],
  },
  batchManifest: {
    container: "batch",
    version: "2.0",
    description: "Cohort batch archive (stub)",
    entries: [
      {
        entry: "run1.msz",
        original: "run1.mzML",
        size: 931_811,
        num_spectra: 50,
        join_key: "scan_number",
        annotations: [],
      },
      {
        entry: "run2.msz",
        original: "run2.mzML",
        size: 909_920,
        num_spectra: 100,
        join_key: "scan_number",
        annotations: [
          {
            filename: "run2.pin",
            format: "percolator_tsv",
            compressed: true,
            num_records: 64,
            description: null,
          },
        ],
      },
    ],
  },
  queue: {
    running: 1,
    paused: false,
    maxConcurrency: 2,
    jobs: [
      {
        id: "q1",
        filePath: mzML,
        fileName: "test.mzML",
        kind: "mzML",
        op: "compress",
        status: "done",
        progress: 100,
        sizeBytes: 4_946_404,
        ratio: 0.28,
        destinationId: "local-archive",
      },
      {
        id: "q2",
        filePath: "/fixtures/sciex_ttof6600_100.mzML",
        fileName: "sciex_ttof6600_100.mzML",
        kind: "mzML",
        op: "compress",
        status: "running",
        progress: 42,
        sizeBytes: 909_920,
      },
    ],
  },
}

/**
 * Installed via page.evaluateOnNewDocument(installApiStub, fixtures). MUST be
 * fully self-contained (serialized to a string and run in the browser) — it may
 * only reference its `fx` argument, never module-scope variables.
 */
export function installApiStub(fx) {
  const settingsListeners = []
  const queueListeners = []
  let settings = { ...fx.settings }

  const summaryFor = (path) => {
    const direct = fx.files[path]
    if (direct) return { ...direct }
    const hit = Object.values(fx.files).find((f) => path.endsWith(f.fileName))
    if (hit) return { ...hit, path }
    // Unknown path → error summary (Inspector renders the failure branch).
    return {
      path,
      fileName: path.split("/").pop() || path,
      kind: "mzML",
      filesizeBytes: 0,
      spectrumCount: null,
      mzFormat: null,
      intensityFormat: null,
      sourceCompression: null,
      msLevelCounts: [],
      rtRangeSec: [0, 0],
      accessions: null,
      error: "unknown fixture",
    }
  }

  window.api = {
    openFiles: () => Promise.resolve(fx.openFilesResult.slice()),
    openOutputDir: () => Promise.resolve("/home/lab/picked"),
    openExternal: () => Promise.resolve(),
    revealInFolder: () => Promise.resolve(),
    getDefaultOutputDir: () => Promise.resolve(fx.defaultOutputDir),
    getVersion: () => Promise.resolve(fx.version),
    getNumThreads: () => Promise.resolve(fx.numThreads),
    getMemory: () =>
      Promise.resolve({
        rssBytes: 412_000_000,
        totalBytes: 34_359_738_368,
        freeBytes: 12_000_000_000,
      }),
    getFilesize: (path) => Promise.resolve(summaryFor(path).filesizeBytes),
    analyze: (path) => Promise.resolve(summaryFor(path)),
    computeQC: (path, opts) => {
      // Record calls so e2e tests can assert per-member QC wiring.
      window.__qcCalls = window.__qcCalls || []
      window.__qcCalls.push({ path, opts: opts || null })
      return Promise.resolve({ ...fx.qc, path })
    },
    readMszx: (path) =>
      Promise.resolve(
        path.endsWith("cohort.mszx") ? { ...fx.batchManifest, path } : { ...fx.manifest, path },
      ),
    getSettings: () => Promise.resolve({ ...settings }),
    setSettings: (partial) => {
      settings = { ...settings, ...partial }
      const snap = { ...settings }
      settingsListeners.forEach((cb) => cb(snap))
      return Promise.resolve(snap)
    },
    onSettingsChange: (cb) => {
      settingsListeners.push(cb)
      return () => {
        const i = settingsListeners.indexOf(cb)
        if (i >= 0) settingsListeners.splice(i, 1)
      }
    },
    setTitleBarOverlay: () => Promise.resolve(),
    compress: (path) =>
      Promise.resolve({
        op: "compress",
        outPath: path.replace(/\.mzML$/, ".msz"),
        inputBytes: 4_946_404,
        outputBytes: 1_384_993,
        ratio: 0.28,
        elapsedMs: 812,
      }),
    decompress: (path) =>
      Promise.resolve({
        op: "decompress",
        outPath: path.replace(/\.msz$/, ".mzML"),
        inputBytes: 952_047,
        outputBytes: 4_946_404,
        ratio: 5.2,
        elapsedMs: 420,
      }),
    extract: (path) =>
      Promise.resolve({
        op: "extract",
        outPath: path.replace(/\.\w+$/, ".subset.mzML"),
        inputBytes: 4_946_404,
        outputBytes: 2_100_000,
        ratio: 0.42,
        elapsedMs: 260,
      }),
    compressBatch: (paths, outPath) => {
      // Record calls so e2e tests can assert the renderer wiring.
      window.__compressBatchCalls = window.__compressBatchCalls || []
      window.__compressBatchCalls.push({ paths: paths.slice(), outPath })
      return Promise.resolve({
        op: "compressBatch",
        outPath,
        inputCount: paths.length,
        inputBytes: 5_856_324,
        outputBytes: 1_841_731,
        ratio: 0.31,
        elapsedMs: 1234,
      })
    },
    onConvertProgress: () => () => {},
    onOpenAssociatedFiles: () => () => {},
    notifyRendererReady: () => {},
    addQueueJob: () => Promise.resolve("job-stub-1"),
    startQueue: () => Promise.resolve(),
    pauseQueue: () => Promise.resolve(),
    clearQueue: () => Promise.resolve(),
    removeJob: () => Promise.resolve(),
    getQueueState: () => Promise.resolve(JSON.parse(JSON.stringify(fx.queue))),
    onQueueUpdate: (cb) => {
      queueListeners.push(cb)
      return () => {
        const i = queueListeners.indexOf(cb)
        if (i >= 0) queueListeners.splice(i, 1)
      }
    },
    getPathForFile: (file) => "/fixtures/" + (file && file.name ? file.name : "dropped"),
  }
}
