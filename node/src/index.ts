export { read } from "@/core/read.js";
export { MZMLFile } from "@/files/mzml-file.js";
export { MSZFile } from "@/files/msz-file.js";
export { BaseFile } from "@/files/base-file.js";
export { Spectrum } from "@/spectrum/spectrum.js";
export { Spectra } from "@/spectrum/spectra.js";
export { Division } from "@/types/division.js";
export { DataPositions } from "@/types/data-positions.js";
export { DataFormat } from "@/types/data-format.js";
export { RuntimeArguments } from "@/types/runtime-arguments.js";
export type { CompressArgs, ExtractOptions } from "@/types/types.js";

// MSZX support
export { MSZXFile } from "@/mszx/mszx-file.js";
export { MSZXBuilder } from "@/mszx/mszx-builder.js";
export { MSZXBatchFile } from "@/mszx/mszx-batch-file.js";
export { MSZXManifest, MSZXBatchManifest, isBatchManifest } from "@/mszx/mszx-manifest.js";
export type {
  AnnotationEntry,
  MSZXManifestData,
  MSZXBatchManifestData,
  SpectraFileEntryData,
} from "@/mszx/mszx-manifest.js";
export { createMSZX } from "@/mszx/mszx.js";
export { MSZXBatchWriter, compressBatch, resolveMzmlInputs } from "@/mszx/mszx-batch-writer.js";
export type {
  BatchInput,
  AddAnnotationOptions,
  CompressBatchOptions,
} from "@/mszx/mszx-batch-writer.js";

import native from "@/core/bindings.js";

export function getNumThreads(): number {
  return native.getNumThreads();
}

export function getFilesize(filePath: string): number {
  return native.getFilesize(filePath);
}

export function getVersion(): string {
  return native.getVersion();
}
