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
  AddPSMsOptions,
  CompressBatchOptions,
} from "@/mszx/mszx-batch-writer.js";

// Search results (PSM annotations) — stored in archives as mzTab
export { PSMCollection, createPSM, createSearchInfo } from "@/annotations/psm.js";
export type {
  PSM,
  SearchInfo,
  ModificationSpec,
  AnnotationFormat,
  ExtraValue,
} from "@/annotations/psm.js";
export { writeMzTab, MzTabReader, parseMzTabParam } from "@/annotations/mztab.js";
export type { MzTabWriteOptions, MzTabParam } from "@/annotations/mztab.js";
export { parsePepXML } from "@/annotations/pepxml.js";
export type { PepXMLOptions } from "@/annotations/pepxml.js";
export { parsePercolator } from "@/annotations/percolator.js";
export type { PercolatorOptions } from "@/annotations/percolator.js";
export {
  readPSMs,
  toMzTab,
  convertToMzTab,
  detectAnnotationFormat,
  mzTabMemberName,
} from "@/annotations/reader.js";
export type {
  PSMSourceFormat,
  ReadPSMsOptions,
  ToMzTabOptions,
  MzTabConversion,
} from "@/annotations/reader.js";

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
