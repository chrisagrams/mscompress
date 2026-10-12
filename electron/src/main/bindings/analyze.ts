import { basename } from "path"
import { getFilesize } from "mscompress"
import type { BaseFile } from "mscompress"
import type { FileSummary } from "../../shared/ipc"
import {
  COMPRESSION,
  MZ_FMT,
  errorMessage,
  fmtLabel,
  kindFromPath,
  msLevelCounts,
  openFile,
} from "./formats"

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
 * Analyze a file and return a rich, plain JSON-serializable summary for the
 * Inspector, from the header DataFormat plus the pre-scanned MS-level /
 * retention-time arrays. Read errors are reported in the `error` field rather
 * than thrown.
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

  let file: BaseFile | null = null
  try {
    file = openFile(path, kind)
    const fmt = file.format
    return {
      ...base,
      filesizeBytes: file.filesize,
      spectrumCount: file.spectra.length,
      mzFormat: fmtLabel(MZ_FMT, fmt.sourceMzFmt),
      intensityFormat: fmtLabel(MZ_FMT, fmt.sourceIntenFmt),
      sourceCompression: fmtLabel(COMPRESSION, fmt.sourceCompression),
      msLevelCounts: msLevelCounts(file.positions.msLevels),
      rtRangeSec: rtRange(file.positions.retTimes),
      accessions: fmt.toDict(),
    }
  } catch (err) {
    return { ...base, filesizeBytes: getFilesize(path), error: errorMessage(err) }
  } finally {
    file?.close()
  }
}
