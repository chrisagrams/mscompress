// Worker-thread entry for native convert operations. Runs in a `worker_threads`
// Worker so the synchronous native `compress`/`decompress`/`extract` calls never
// block the Electron main JS thread. It reuses the exact same self-contained
// wrappers from bindings.ts (which also handle native-addon resolution), so
// there is no duplicated convert logic here — only the message plumbing.
// Batch archiving streams `progress` messages before the final `result`.
import { parentPort } from "worker_threads"
import { compress, compressBatch, decompress, extract } from "./bindings.ts"
import type { ConvertWorkerMessage } from "./bindings.ts"
import type {
  CompressBatchOptions,
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
} from "../shared/ipc.ts"

/** Message posted from the main thread to run one convert job. */
export type ConvertRequest =
  | { op: "compress"; path: string; opts: CompressOptions }
  | { op: "decompress"; path: string; opts: DecompressOptions }
  | { op: "extract"; path: string; opts: ExtractOptions }
  | { op: "compressBatch"; paths: string[]; outPath: string; opts: CompressBatchOptions }

function post(msg: ConvertWorkerMessage): void {
  parentPort!.postMessage(msg)
}

/** Dispatch to the matching wrapper (each already returns ConvertResult). */
async function run(req: ConvertRequest): Promise<ConvertResult> {
  switch (req.op) {
    case "compress":
      return compress(req.path, req.opts)
    case "decompress":
      return decompress(req.path, req.opts)
    case "extract":
      return extract(req.path, req.opts)
    case "compressBatch":
      return compressBatch(req.paths, req.outPath, req.opts, (index, total, file) =>
        post({ type: "progress", index, total, file }),
      )
  }
}

if (parentPort) {
  parentPort.on("message", (req: ConvertRequest) => {
    void run(req).then((result) => post({ type: "result", result }))
  })
}
