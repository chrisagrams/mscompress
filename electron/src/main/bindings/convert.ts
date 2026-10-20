// Convert operations: compress / decompress / extract / compressBatch. These
// are synchronous native calls (compressBatch is async) and run inside the
// convert worker — see worker.ts.
import { mkdirSync, statSync } from "fs"
import { basename, dirname, extname, join } from "path"
import { hrtime } from "process"
import {
  MSZXBatchFile,
  compressBatch as nativeCompressBatch,
  getFilesize,
  getNumThreads,
  read,
} from "mscompress"
import type { BaseFile, ExtractOptions as NativeExtractOptions } from "mscompress"
import { COMPRESSION_PRESETS } from "../../shared/ipc"
import type {
  CompressBatchOptions,
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
} from "../../shared/ipc"
import { LOSSY_FORMAT, errorMessage, kindFromPath, openFile } from "./formats"

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

export function errorResult(op: ConvertResult["op"], err: unknown): ConvertResult {
  return {
    op,
    outPath: "",
    inputBytes: 0,
    outputBytes: 0,
    ratio: 0,
    elapsedMs: 0,
    error: errorMessage(err),
  }
}

/** Apply preset + advanced overrides onto a file's RuntimeArguments. */
function applyCompressArgs(file: BaseFile, opts: CompressOptions): void {
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
  let file: BaseFile | null = null
  try {
    if (kindFromPath(path) !== "mzML") {
      throw new Error("Only mzML files can be compressed.")
    }
    file = read(path)
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

/**
 * Expand a batch (v2) .mszx into `<outputDir>/<archive stem>/<entry>.mzML`.
 * Returns null when the archive is a v1 single-file .mszx, so the caller falls
 * through to the flat single-output path.
 */
function decompressBatchArchive(
  path: string,
  opts: DecompressOptions,
  startedAt: bigint,
): ConvertResult | null {
  const bf = MSZXBatchFile.open(path)
  try {
    if (bf.manifest.container !== "batch") return null
    const baseDir = opts.outputDir && opts.outputDir.length > 0 ? opts.outputDir : dirname(path)
    const outDir = join(baseDir, stripExt(basename(path)))
    mkdirSync(outDir, { recursive: true })
    const threads = opts.threads ?? getNumThreads()
    const written: string[] = []
    for (const entry of bf.manifest.spectra_files) {
      // Members are owned by the archive (closed by bf.close()); the returned
      // output handle is ours to close.
      const member = bf.get(entry.entry)
      member.arguments.threads = threads
      const target = join(outDir, `${stripExt(entry.entry)}.mzML`)
      const out = member.decompress(target)
      out.close()
      written.push(target)
    }
    const inputBytes = getFilesize(path)
    const outputBytes = written.reduce((sum, p) => sum + statSync(p).size, 0)
    return {
      op: "decompress",
      outPath: outDir,
      outPaths: written,
      inputBytes,
      outputBytes,
      ratio: inputBytes > 0 ? outputBytes / inputBytes : 0,
      elapsedMs: Number(hrtime.bigint() - startedAt) / 1e6,
    }
  } finally {
    bf.close()
  }
}

/** Decompress an .msz/.mszx → .mzML (a batch .mszx → a directory of .mzML). */
export function decompress(path: string, opts: DecompressOptions): ConvertResult {
  const startedAt = hrtime.bigint()
  const kind = kindFromPath(path)
  let file: BaseFile | null = null
  try {
    if (kind === "mzML") {
      throw new Error("mzML files are already decompressed.")
    }
    if (kind === "mszx") {
      const batchResult = decompressBatchArchive(path, opts, startedAt)
      if (batchResult) return batchResult
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

/**
 * Compress many mzML files into one batch (.mszx v2) archive at `outPath`.
 * Async because the underlying incremental writer is; always resolves with a
 * ConvertResult (errors reported structurally — on failure the native writer
 * removes the partial archive). `onProgress` fires before each entry.
 */
export async function compressBatch(
  paths: string[],
  outPath: string,
  opts: CompressBatchOptions,
  onProgress?: (index: number, total: number, file: string) => void,
): Promise<ConvertResult> {
  const startedAt = hrtime.bigint()
  try {
    if (paths.length === 0) {
      throw new Error("No input files to archive.")
    }
    const notMzml = paths.find((p) => kindFromPath(p) !== "mzML")
    if (notMzml) {
      throw new Error(`Only mzML files can be archived: ${basename(notMzml)}`)
    }
    const preset = COMPRESSION_PRESETS[opts.preset] ?? COMPRESSION_PRESETS.default
    const mz = opts.mzLossy ?? preset.mzLossy
    const int = opts.intLossy ?? preset.intLossy
    await nativeCompressBatch(paths, outPath, {
      threads: opts.threads ?? getNumThreads(),
      zstdCompressionLevel: opts.zstdLevel ?? preset.zstdLevel,
      targetMzFormat: LOSSY_FORMAT[mz],
      targetIntenFormat: LOSSY_FORMAT[int],
      description: opts.description,
      onProgress,
    })
    const inputBytes = paths.reduce((sum, p) => sum + getFilesize(p), 0)
    const outputBytes = statSync(outPath).size
    return {
      op: "compressBatch",
      outPath,
      inputCount: paths.length,
      inputBytes,
      outputBytes,
      ratio: inputBytes > 0 ? outputBytes / inputBytes : 0,
      elapsedMs: Number(hrtime.bigint() - startedAt) / 1e6,
    }
  } catch (err) {
    return errorResult("compressBatch", err)
  }
}

/** Translate the UI extract options into the native filter object. */
function nativeExtractOptions(opts: ExtractOptions): NativeExtractOptions {
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
  let file: BaseFile | null = null
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
