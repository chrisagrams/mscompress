// Async offload: run the (synchronous) native convert in a worker thread so it
// never blocks the Electron main JS thread. One-shot worker per job — a pool is
// overkill at maxConcurrency 1.
import { Worker } from "worker_threads"
import type {
  CompressBatchOptions,
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
} from "../../shared/ipc"
import { errorResult } from "./convert"

/** Messages a convert worker posts back: streamed progress, then one result. */
export type ConvertWorkerMessage =
  | { type: "progress"; index: number; total: number; file: string }
  | { type: "result"; result: ConvertResult }

/**
 * Run a request in a one-shot worker. Always resolves with a ConvertResult —
 * worker/spawn failures are mapped to an `errorResult`-shaped value (never
 * thrown). `onProgress` relays the worker's streamed per-file progress (batch
 * ops only).
 */
function runInWorker(
  op: ConvertResult["op"],
  request: unknown,
  onProgress?: (index: number, total: number, file: string) => void,
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
      // The built worker chunk sits next to this module's chunk in out/main/.
      worker = new Worker(new URL("./convert-worker.js", import.meta.url))
    } catch (err) {
      resolve(errorResult(op, err))
      return
    }
    worker.on("message", (msg: ConvertWorkerMessage) => {
      if (msg.type === "progress") {
        onProgress?.(msg.index, msg.total, msg.file)
        return
      }
      finish(msg.result)
    })
    worker.once("error", (err) => finish(errorResult(op, err)))
    worker.once("exit", (code) => {
      if (code !== 0) finish(errorResult(op, new Error(`Convert worker exited with code ${code}`)))
    })
    worker.postMessage(request)
  })
}

/** Run a single-file convert off the main thread. */
export function runConvertInWorker(
  op: "compress" | "decompress" | "extract",
  path: string,
  opts: CompressOptions | DecompressOptions | ExtractOptions,
): Promise<ConvertResult> {
  return runInWorker(op, { op, path, opts })
}

/** Run a batch (.mszx v2) archive build off the main thread. */
export function runBatchCompressInWorker(
  paths: string[],
  outPath: string,
  opts: CompressBatchOptions,
  onProgress?: (index: number, total: number, file: string) => void,
): Promise<ConvertResult> {
  return runInWorker("compressBatch", { op: "compressBatch", paths, outPath, opts }, onProgress)
}
