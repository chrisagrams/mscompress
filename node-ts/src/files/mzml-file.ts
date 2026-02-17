import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import native from "../core/bindings.js";
import { BaseFile } from "./base-file.js";
import { DataFormat } from "../types/data-format.js";
import { Division } from "../types/division.js";
import { createFile, registerFileFactory } from "./file-registry.js";
import type { ExtractOptions } from "../types/types.js";

/**
 * Handler for uncompressed mzML files.
 * Supports compression, extraction, and spectrum access.
 */
export class MZMLFile extends BaseFile {
  /**
   * Create an MZMLFile instance.
   *
   * @param filePath - Path to the mzML file
   */
  constructor(filePath: string) {
    super(filePath);
    // Detect data format (pattern_detect for mzML)
    const dfNative = native.getDataFormat(this._handle);
    this._format = new DataFormat(dfNative);

    // Scan positions
    const posNative = native.scanMzml(this._handle);
    this._positions = new Division(posNative);

    // Set compress runtime variables
    native.setCompressRuntimeVars(this._handle, this._arguments.toNative());
  }

  /**
   * Data format information.
   */
  get format(): DataFormat {
    if (!this._format) {
      throw new Error("Format not initialized");
    }
    return this._format;
  }

  /**
   * Position information for spectra.
   */
  get positions(): Division {
    if (!this._positions) {
      throw new Error("Positions not initialized");
    }
    return this._positions;
  }

  /**
   * Get m/z binary array for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns m/z array
   */
  getMzBinary(index: number): Float64Array | Float32Array {
    return native.getMzBinaryMzml(this._handle, index, this._arguments.blocksize);
  }

  /**
   * Get intensity binary array for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns Intensity array
   */
  getIntenBinary(index: number): Float64Array | Float32Array {
    return native.getIntenBinaryMzml(this._handle, index, this._arguments.blocksize);
  }

  /**
   * Get XML for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns XML string
   */
  getXml(index: number): string {
    return native.getXmlMzml(this._handle, index);
  }

  /**
   * Compress mzML to MSZ format.
   *
   * @param output - Output MSZ file path
   * @returns MSZFile instance for the compressed file
   */
  compress(output: string): BaseFile {
    const outputPath = path.resolve(output);

    // Prepare divisions for compression
    native.prepareDivisions(this._handle, this._arguments.toNative());

    // Run compression
    native.compressMzml(this._handle, outputPath, this._arguments.toNative());

    return createFile("msz", outputPath);
  }

  /**
   * Extract filtered spectra to a new file.
   *
   * @param output - Output file path (.mzML or .msz)
   * @param options - Optional extraction filters (indices, scanNumbers, msLevel)
   * @returns File instance for the extracted data
   * @throws Error if output extension is not .mzML or .msz
   */
  extract(output: string, options?: ExtractOptions): BaseFile {
    const outputPath = path.resolve(output);
    const ext = path.extname(outputPath).toLowerCase();

    if (ext === ".msz") {
      const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-"));
      const tmpMzml = path.join(tmpDir, "temp.mzML");

      try {
        this.extract(tmpMzml, options);
        const tmpFile = new MZMLFile(tmpMzml);
        try {
          return tmpFile.compress(outputPath);
        } finally {
          tmpFile.close();
        }
      } finally {
        fs.rmSync(tmpDir, { recursive: true, force: true });
      }
    } else if (ext === ".mzml") {
      native.extractMzmlFiltered(this._handle, outputPath, options ?? {});
      return new MZMLFile(outputPath);
    } else {
      throw new Error(`Unsupported output file extension: ${ext}. Use .msz or .mzML`);
    }
  }

  /**
   * Decompress operation not supported for mzML files.
   *
   * @throws Error always
   */
  decompress(_output: string): never {
    throw new Error("Cannot decompress an mzML file.");
  }
}

// Register factory
registerFileFactory("mzml", (p) => new MZMLFile(p));
