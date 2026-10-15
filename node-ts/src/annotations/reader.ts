/**
 * Format-agnostic PSM reading and conversion to mzTab.
 *
 * MSZX archives store search results only as mzTab; these helpers turn any
 * supported engine output (pepXML, Percolator TSV/PIN) into mzTab on the way
 * in, and let legacy archive members (written before mzTab became the only
 * stored format) be queried through the same API.
 */

import * as path from "node:path";
import native from "@/core/bindings.js";
import { MzTabReader, writeMzTab } from "@/annotations/mztab.js";
import { parsePepXML, type PepXMLOptions } from "@/annotations/pepxml.js";
import { parsePercolator, type PercolatorOptions } from "@/annotations/percolator.js";
import { PSMCollection, type AnnotationFormat, type SearchInfo } from "@/annotations/psm.js";

/** Concrete parser formats ({@link AnnotationFormat} minus the legacy `"tsv"` alias). */
export type PSMSourceFormat = "mztab" | "pepxml" | "percolator_tsv";

/** Options for {@link readPSMs}. */
export interface ReadPSMsOptions extends PepXMLOptions, PercolatorOptions {}

/** Options for {@link toMzTab}. */
export interface ToMzTabOptions extends ReadPSMsOptions {
  /** Input format; auto-detected when omitted. */
  format?: AnnotationFormat;
  /** Spectra file name for `ms_run[1]-location`. */
  msRunLocation?: string | null;
  /** Original file name recorded in the MTD; defaults to `nameHint` minus `.zst`. */
  sourceName?: string | null;
  /** Written as `mzTab-ID`. */
  title?: string | null;
  /** Overrides the search metadata recovered from the input. */
  searchInfo?: SearchInfo | null;
}

/** Result of {@link convertToMzTab}. */
export interface MzTabConversion {
  /** The mzTab document. */
  text: string;
  /** Number of PSMs it contains. */
  numRecords: number;
  /** Format of the input. */
  inputFormat: PSMSourceFormat;
}

const ZSTD_MAGIC = Buffer.from([0x28, 0xb5, 0x2f, 0xfd]);

/** Decode input to text, transparently decompressing zstd frames. */
function toText(data: Buffer | string): string {
  if (typeof data === "string") return data;
  const bytes =
    data.length >= 4 && data.subarray(0, 4).equals(ZSTD_MAGIC) ? native.zstdDecompress(data) : data;
  return bytes.toString("utf-8");
}

/** Strip a trailing `.zst` (case-insensitive). */
export function stripZst(name: string): string {
  return name.toLowerCase().endsWith(".zst") ? name.slice(0, -4) : name;
}

/** Normalize a manifest/user format tag to a parser format. */
function normalizeFormat(format: string): PSMSourceFormat {
  switch (format) {
    case "mztab":
    case "pepxml":
    case "percolator_tsv":
      return format;
    case "tsv":
    case "pin":
      return "percolator_tsv";
    default:
      throw new Error(`Unsupported annotation format: ${format}`);
  }
}

/**
 * Detect the format of an annotation from its name, then its content.
 *
 * @param text - Annotation text.
 * @param nameHint - File/member name (a trailing `.zst` is ignored).
 * @returns The detected format.
 * @throws Error if the format cannot be determined.
 */
export function detectAnnotationFormat(text: string, nameHint?: string | null): PSMSourceFormat {
  if (nameHint) {
    const name = stripZst(path.basename(nameHint)).toLowerCase();
    if (name.endsWith(".mztab")) return "mztab";
    if (name.endsWith(".pin") || name.endsWith(".tsv")) return "percolator_tsv";
    if (name.endsWith(".pepxml") || name.endsWith(".pep.xml")) return "pepxml";
  }
  if (text.startsWith("MTD\t") || text.includes("\nPSH\t")) return "mztab";
  const head = text.slice(0, 1024);
  if (head.includes("pepXML") || head.includes("spectrum_query")) return "pepxml";
  if (head.includes("SpecId") || head.includes("PSMId") || head.includes("ScanNr")) {
    return "percolator_tsv";
  }
  throw new Error(
    `Could not determine annotation format${nameHint ? ` of '${nameHint}'` : ""}; ` +
      "expected mzTab, pepXML or Percolator TSV/PIN"
  );
}

function parseAs(text: string, format: PSMSourceFormat, options?: ReadPSMsOptions): PSMCollection {
  if (format === "mztab") return new MzTabReader(text);
  const { psms, searchInfo } =
    format === "pepxml" ? parsePepXML(text, options) : parsePercolator(text, options);
  return new PSMCollection(psms, searchInfo, format);
}

/**
 * Parse PSMs from any supported annotation format.
 *
 * @param data - Annotation content (zstd-compressed buffers are decompressed).
 * @param nameHint - File/member name, used for format detection.
 * @param format - Explicit format; skips detection. `"tsv"` reads as Percolator.
 * @param options - Parser options (rank cutoff, decoy prefix).
 * @returns A PSM collection; an {@link MzTabReader} for mzTab input.
 */
export function readPSMs(
  data: Buffer | string,
  nameHint?: string | null,
  format?: AnnotationFormat | null,
  options?: ReadPSMsOptions
): PSMCollection {
  const text = toText(data);
  const fmt = format ? normalizeFormat(format) : detectAnnotationFormat(text, nameHint);
  return parseAs(text, fmt, options);
}

/**
 * Convert an annotation to mzTab, also reporting its PSM count.
 *
 * mzTab input is passed through unchanged.
 *
 * @param data - Annotation content (zstd-compressed buffers are decompressed).
 * @param nameHint - File/member name, used for detection and as the default source name.
 * @param options - Conversion options.
 * @returns mzTab text, PSM count and the detected input format.
 */
export function convertToMzTab(
  data: Buffer | string,
  nameHint?: string | null,
  options?: ToMzTabOptions
): MzTabConversion {
  const text = toText(data);
  const fmt = options?.format
    ? normalizeFormat(options.format)
    : detectAnnotationFormat(text, nameHint);
  const collection = parseAs(text, fmt, options);
  if (fmt === "mztab") {
    return { text, numRecords: collection.length, inputFormat: fmt };
  }
  const sourceName =
    options?.sourceName !== undefined
      ? options.sourceName
      : nameHint
        ? stripZst(path.basename(nameHint))
        : null;
  const mztab = writeMzTab(collection.psms, {
    searchInfo: options?.searchInfo ?? collection.searchInfo,
    msRunLocation: options?.msRunLocation ?? null,
    sourceFormat: fmt,
    sourceName,
    title: options?.title ?? null,
  });
  return { text: mztab, numRecords: collection.length, inputFormat: fmt };
}

/**
 * Convert any supported annotation into mzTab text.
 *
 * @param data - Annotation content (zstd-compressed buffers are decompressed).
 * @param nameHint - File/member name, used for detection and as the default source name.
 * @param options - Conversion options.
 * @returns The mzTab document (mzTab input is returned unchanged).
 */
export function toMzTab(
  data: Buffer | string,
  nameHint?: string | null,
  options?: ToMzTabOptions
): string {
  return convertToMzTab(data, nameHint, options).text;
}

/**
 * Archive member name for an annotation stored as mzTab:
 * `<name>.mztab`, plus `.zst` when compressed. The name is reused as-is when
 * the source already is mzTab.
 *
 * @param name - Original annotation file name (a trailing `.zst` is ignored).
 * @param compressed - Whether the member is zstd-compressed.
 * @param isMzTab - Whether the source is already mzTab (defaults to a `.mztab` name check).
 */
export function mzTabMemberName(name: string, compressed: boolean, isMzTab?: boolean): string {
  const base = stripZst(path.basename(name));
  const alreadyMzTab = isMzTab ?? base.toLowerCase().endsWith(".mztab");
  const mztab = alreadyMzTab ? base : `${base}.mztab`;
  return compressed ? `${mztab}.zst` : mztab;
}
