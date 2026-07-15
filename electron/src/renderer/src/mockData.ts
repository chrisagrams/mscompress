// Shared mock data for MScompress GUI mockups.
// No native bindings — this mirrors the shape returned by the `mscompress` node-ts
// package (read()/MZMLFile/MSZFile/MSZXFile) so a mockup looks/behaves realistically.

export type MsFormat = "float64" | "float32"
export type SourceCompression = "none" | "zlib" | "zstd"
export type LossyAlgo = "none" | "cast" | "log" | "delta16" | "delta32" | "vbr"
export type Profile = "fastest" | "fast" | "default" | "better"
export type FileKind = "mzML" | "msz" | "mszx"

export interface FileAnalysis {
  fileName: string
  kind: FileKind
  filesizeBytes: number
  spectrumCount: number
  mzFormat: MsFormat
  intensityFormat: MsFormat
  sourceCompression: SourceCompression
  msLevelCounts: { level: string; count: number }[] // MS1, MS2, MSn
  rtRangeSec: [number, number]
  threads: number
}

export interface QueueItem {
  id: string
  fileName: string
  kind: FileKind
  sizeBytes: number
  status: "queued" | "running" | "done" | "error"
  progress: number // 0..100
  op: "compress" | "decompress" | "extract"
  ratio?: number // out/in for done compress items
  etaSec?: number
}

// ---- Analysis panel sample ----
export const sampleAnalysis: FileAnalysis = {
  fileName: "HEK293_rep1.mzML",
  kind: "mzML",
  filesizeBytes: 2_411_724_800, // ~2.25 GB
  spectrumCount: 84213,
  mzFormat: "float64",
  intensityFormat: "float32",
  sourceCompression: "zlib",
  msLevelCounts: [
    { level: "MS1", count: 14980 },
    { level: "MS2", count: 68201 },
    { level: "MSn", count: 1032 },
  ],
  rtRangeSec: [0, 5400],
  threads: 8,
}

// ---- TIC chromatogram (intensity vs RT) ----
export const ticChromatogram: { rt: number; tic: number }[] = Array.from(
  { length: 180 },
  (_, i) => {
    const rt = (i / 180) * 90 // minutes
    const base = 2e6 + 1.5e6 * Math.sin(rt / 6)
    const peaks =
      6e6 * Math.exp(-((rt - 32) ** 2) / 12) +
      9e6 * Math.exp(-((rt - 54) ** 2) / 8) +
      4e6 * Math.exp(-((rt - 71) ** 2) / 10)
    const noise = 4e5 * Math.random()
    return { rt: +rt.toFixed(2), tic: Math.max(0, base + peaks + noise) }
  },
)

// ---- Base-peak chromatogram ----
export const basePeakChromatogram: { rt: number; bpc: number }[] =
  ticChromatogram.map((p) => ({ rt: p.rt, bpc: p.tic * (0.4 + 0.3 * Math.random()) }))

// ---- RT x m/z density heatmap (QC). Rows = m/z bins, cols = RT bins ----
export const heatmap = (() => {
  const rtBins = 60
  const mzBins = 40
  const mzMin = 300
  const mzMax = 1500
  const cells: { rt: number; mz: number; density: number }[] = []
  for (let r = 0; r < rtBins; r++) {
    for (let m = 0; m < mzBins; m++) {
      const rt = (r / rtBins) * 90
      const mz = mzMin + (m / mzBins) * (mzMax - mzMin)
      const band = Math.exp(-((mz - 650) ** 2) / 60000) + 0.6 * Math.exp(-((mz - 1050) ** 2) / 40000)
      const elut = Math.exp(-((rt - 45) ** 2) / 500) + 0.5
      const d = band * elut * (0.6 + 0.8 * Math.random())
      cells.push({ rt: +rt.toFixed(1), mz: Math.round(mz), density: +d.toFixed(3) })
    }
  }
  return { rtBins, mzBins, mzMin, mzMax, rtMax: 90, cells }
})()

// ---- Peaks-per-spectrum distribution (QC histogram) ----
export const peaksPerSpectrum: { bin: string; count: number }[] = [
  { bin: "0-100", count: 3200 },
  { bin: "100-250", count: 9800 },
  { bin: "250-500", count: 21400 },
  { bin: "500-1k", count: 28900 },
  { bin: "1k-2k", count: 15600 },
  { bin: "2k-4k", count: 4200 },
  { bin: "4k+", count: 1113 },
]

// ---- MSTransfer batch queue sample ----
export const sampleQueue: QueueItem[] = [
  { id: "q1", fileName: "HEK293_rep1.mzML", kind: "mzML", sizeBytes: 2_411_724_800, status: "done", progress: 100, op: "compress", ratio: 0.28 },
  { id: "q2", fileName: "HEK293_rep2.mzML", kind: "mzML", sizeBytes: 2_388_100_000, status: "running", progress: 63, op: "compress", etaSec: 41 },
  { id: "q3", fileName: "Plasma_DIA_01.mzML", kind: "mzML", sizeBytes: 5_120_400_000, status: "running", progress: 21, op: "compress", etaSec: 190 },
  { id: "q4", fileName: "QC_pool_A.mzML", kind: "mzML", sizeBytes: 1_002_000_000, status: "queued", progress: 0, op: "compress" },
  { id: "q5", fileName: "archive_2023.msz", kind: "msz", sizeBytes: 640_200_000, status: "queued", progress: 0, op: "decompress" },
  { id: "q6", fileName: "bad_file.mzML", kind: "mzML", sizeBytes: 12_000_000, status: "error", progress: 0, op: "compress" },
]

// ---- MSZX manifest sample (archive with annotations) ----
export const sampleMszxManifest = {
  version: "1.0",
  created_at: "2026-05-12T14:03:22Z",
  spectra_file: "spectra.msz",
  num_spectra: 84213,
  join_key: "scan_number",
  source_file: "HEK293_rep1.mzML",
  description: "HEK293 tryptic digest, Orbitrap, DDA",
  annotations: [
    { filename: "psms.percolator.tsv", format: "percolator_tsv", compressed: true, num_records: 41288, description: "Percolator PSMs (q<0.01)" },
    { filename: "peptides.pep.xml", format: "pepxml", compressed: false, num_records: 38102, description: "Comet search results" },
  ],
}

export const compressionProfiles: { id: Profile; label: string; blurb: string }[] = [
  { id: "fastest", label: "Fastest", blurb: "Lowest latency, larger output" },
  { id: "fast", label: "Fast", blurb: "Quick, good ratio" },
  { id: "default", label: "Default", blurb: "Balanced (recommended)" },
  { id: "better", label: "Better", blurb: "Smallest output, slower" },
]

export function fmtBytes(n: number): string {
  const u = ["B", "KB", "MB", "GB", "TB"]
  let i = 0
  let v = n
  while (v >= 1024 && i < u.length - 1) { v /= 1024; i++ }
  return `${v.toFixed(v < 10 && i > 0 ? 1 : 0)} ${u[i]}`
}
