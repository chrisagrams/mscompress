import { MSZXBatchFile } from "mscompress"
import type { BaseFile } from "mscompress"
import type { QCData, QCOptions } from "../../shared/ipc"
import { errorMessage, kindFromPath, msLevelCounts, openFile } from "./formats"

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

  let file: BaseFile | null = null
  let archive: MSZXBatchFile | null = null
  try {
    if (kind === "mszx" && opts.entry) {
      // Batch (v2) archive member: mmap the embedded MSZ at its offset. The
      // member is owned by the archive — close the archive, not the member.
      archive = MSZXBatchFile.open(path)
      file = archive.get(opts.entry)
    } else {
      file = openFile(path, kind)
    }
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
    return { ...base, error: errorMessage(err) }
  } finally {
    if (archive) archive.close()
    else file?.close()
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
