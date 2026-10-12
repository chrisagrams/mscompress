import fs from "node:fs";
import path from "node:path";
import type { BaseFile } from "@/files/base-file.js";
// Import to trigger factory registration
import { MZMLFile } from "@/files/mzml-file.js";
import { MSZFile } from "@/files/msz-file.js";
import { MSZXBatchFile, readArchiveManifestJSON } from "@/mszx/mszx-batch-file.js";
import { MSZXFile } from "@/mszx/mszx-file.js";
import { isBatchManifest } from "@/mszx/mszx-manifest.js";

const MAGIC_TAG = 0x035f51b5;

/** Offset of the "ustar" magic in a POSIX tar header block. */
const USTAR_MAGIC_OFFSET = 257;

/**
 * Detect file type by reading file header.
 *
 * @param filePath - Path to the file to detect
 * @returns File type ('mzML', 'msz' or 'mszx') or null if unknown
 */
function detectFiletype(filePath: string): "mzML" | "msz" | "mszx" | null {
  const fd = fs.openSync(filePath, "r");
  const buf = Buffer.alloc(512);
  try {
    fs.readSync(fd, buf, 0, 512, 0);
  } finally {
    fs.closeSync(fd);
  }

  // Check for MSZ magic tag (little-endian)
  if (buf.length >= 4) {
    const magic = buf.readUInt32LE(0);
    if (magic === MAGIC_TAG) {
      return "msz";
    }
  }

  // Check for mzML: "indexedmzML" wrapper, "<mzML" root element, or the mzML
  // namespace URI (non-indexed mzML, e.g. Waters exports, has no
  // "indexedmzML" wrapper)
  // A .mszx is an uncompressed POSIX tar, so it matches neither the MSZ magic
  // nor the mzML markers. Sniff the USTAR magic before falling through.
  if (buf.length >= USTAR_MAGIC_OFFSET + 5 &&
      buf.toString("latin1", USTAR_MAGIC_OFFSET, USTAR_MAGIC_OFFSET + 5) === "ustar") {
    return "mszx";
  }

  const text = buf.toString("utf-8");
  if (
    text.includes("indexedmzML") ||
    text.includes("<mzML") ||
    text.includes("http://psi.hupo.org/ms/mzml")
  ) {
    return "mzML";
  }

  return null;
}

/**
 * Read and parse mzML or MSZ files.
 * Auto-detects file type and returns the appropriate class.
 *
 * @param filePath - Path to the mzML or MSZ file
 * @returns MZMLFile or MSZFile instance
 * @throws TypeError if path is not a string
 * @throws Error if file doesn't exist, is a directory, or type cannot be determined
 *
 * @example
 * ```typescript
 * const file = read('sample.mzML');
 * console.log(`Loaded ${file.spectra.length} spectra`);
 * file.close();
 * ```
 */
export function read(filePath: string): BaseFile {
  if (typeof filePath !== "string") {
    throw new TypeError("Path must be a string.");
  }

  const resolved = path.resolve(filePath);

  if (!fs.existsSync(resolved)) {
    throw new Error(`The specified file does not exist: ${resolved}`);
  }

  const stat = fs.statSync(resolved);
  if (stat.isDirectory()) {
    throw new Error(`The specified path is a directory, not a file: ${resolved}`);
  }

  const filetype = detectFiletype(resolved);

  if (filetype === "mzML") {
    return new MZMLFile(resolved);
  } else if (filetype === "msz") {
    return new MSZFile(resolved);
  } else if (filetype === "mszx") {
    // v1 (single spectra_file) and v2 (multi-file "batch") archives share the
    // extension and both carry manifest.json, so the manifest decides which
    // reader to build. A batch archive holds N payloads and therefore cannot
    // be an MSZFile the way MSZXFile is.
    const raw: unknown = JSON.parse(readArchiveManifestJSON(resolved));
    if (typeof raw !== "object" || raw === null) {
      throw new TypeError("Invalid manifest JSON: expected object");
    }
    return isBatchManifest(raw as Record<string, unknown>)
      ? (MSZXBatchFile.open(resolved) as unknown as BaseFile)
      : MSZXFile.open(resolved);
  } else {
    throw new Error(`Could not determine file type for: ${resolved}`);
  }
}
