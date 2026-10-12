// Main-process wrappers around the native `mscompress` node-ts binding. This
// directory is the ONLY place the addon is imported, and it must never be
// imported from the renderer (it is externalized from the Vite bundle).
export { getFilesize, getNumThreads, getVersion } from "mscompress"
export { analyze } from "./analyze"
export { readMszx } from "./mszx"
export { computeQC } from "./qc"
export { runBatchCompressInWorker, runConvertInWorker } from "./worker"
