import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import native from "@/core/bindings.js";
import { BaseFile } from "@/files/base-file.js";
import { DataFormat } from "@/types/data-format.js";
import { Division } from "@/types/division.js";
import { createFile, registerFileFactory } from "@/files/file-registry.js";
import type { ExtractOptions } from "@/types/types.js";

/**
 * Handler for compressed MSZ files.
 * Supports decompression, extraction, and random-access spectrum reading.
 */
export class MSZFile extends BaseFile {
  /**
   * Create an MSZFile instance.
   *
   * @param filePath - Path to the MSZ file
   */
  constructor(filePath: string) {
    super(filePath);
    // Read header data format
    const dfNative = native.getDataFormat(this._handle);
    this._format = new DataFormat(dfNative);

    // Read divisions, footer, block lengths from MSZ footer
    const result = native.readMszDivisions(this._handle);
    this._positions = new Division(result.positions);

    // Set decompress runtime variables
    native.setDecompressRuntimeVars(this._handle);
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
   * Get m/z binary array for a spectrum with random access.
   *
   * @param index - Zero-based spectrum index
   * @returns m/z array
   */
  getMzBinary(index: number): Float64Array | Float32Array {
    return native.getMzBinaryMsz(this._handle, index);
  }

  /**
   * Get intensity binary array for a spectrum with random access.
   *
   * @param index - Zero-based spectrum index
   * @returns Intensity array
   */
  getIntenBinary(index: number): Float64Array | Float32Array {
    return native.getIntenBinaryMsz(this._handle, index);
  }

  /**
   * Get XML for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns XML string
   */
  getXml(index: number): string {
    return native.getXmlMsz(this._handle, index);
  }

  /**
   * Decompress MSZ to mzML format.
   *
   * @param output - Output mzML file path
   * @returns MZMLFile instance for the decompressed file
   */
  decompress(output: string): BaseFile {
    const outputPath = path.resolve(output);
    native.decompressMsz(this._handle, outputPath, this._arguments.toNative());
    return createFile("mzml", outputPath);
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
        const tmpMzmlFile = this.extract(tmpMzml, options);
        try {
          return tmpMzmlFile.compress(outputPath);
        } finally {
          tmpMzmlFile.close();
        }
      } finally {
        fs.rmSync(tmpDir, { recursive: true, force: true });
      }
    } else if (ext === ".mzml") {
      native.extractMsz(this._handle, outputPath, options ?? {});
      return createFile("mzml", outputPath);
    } else {
      throw new Error(`Unsupported output file extension: ${ext}. Use .msz or .mzML`);
    }
  }

  /**
   * Compress operation not supported for MSZ files (already compressed).
   *
   * @throws Error always
   */
  compress(_output: string): never {
    throw new Error("Cannot compress an MSZ file.");
  }
}

// Register factory
registerFileFactory("msz", (p) => new MSZFile(p));
