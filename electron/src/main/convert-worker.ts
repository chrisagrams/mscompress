// Worker-thread entry for native convert operations. Runs in a `worker_threads`
// Worker so the synchronous native `compress`/`decompress`/`extract` calls never
// block the Electron main JS thread. It reuses the exact same self-contained
// wrappers from bindings.ts (which also handle native-addon resolution), so
// there is no duplicated convert logic here — only the message plumbing.
import { parentPort } from "worker_threads"
import { compress, decompress, extract } from "./bindings.ts"
import type {
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
} from "../shared/ipc.ts"

/** Message posted from the main thread to run one convert job. */
export interface ConvertRequest {
  op: "compress" | "decompress" | "extract"
  path: string
  opts: CompressOptions | DecompressOptions | ExtractOptions
}

/** Dispatch to the matching sync wrapper (each already returns ConvertResult). */
function run(req: ConvertRequest): ConvertResult {
  switch (req.op) {
    case "compress":
      return compress(req.path, req.opts as CompressOptions)
    case "decompress":
      return decompress(req.path, req.opts as DecompressOptions)
    case "extract":
      return extract(req.path, req.opts as ExtractOptions)
  }
}

if (parentPort) {
  parentPort.on("message", (req: ConvertRequest) => {
    parentPort!.postMessage(run(req))
  })
}
