import native from "../core/bindings.js";
import type { NativeFileHandle } from "../core/bindings.js";
import type { ExtractOptions } from "../types/types.js";
import { DataFormat } from "../types/data-format.js";
import { Division } from "../types/division.js";
import { Spectra } from "../spectrum/spectra.js";
import { RuntimeArguments } from "../types/runtime-arguments.js";

/**
 * Abstract base class for mass spectrometry file handlers.
 * Provides common functionality for MZMLFile and MSZFile.
 */
export abstract class BaseFile {
  readonly path: string;
  readonly filesize: number;
  protected _handle: NativeFileHandle;
  protected _format: DataFormat | null = null;
  protected _positions: Division | null = null;
  protected _spectra: Spectra | null = null;
  protected _arguments: RuntimeArguments;

  /**
   * Create a file instance.
   *
   * @param filePath - Absolute path to the file
   */
  constructor(filePath: string) {
    this.path = filePath;
    this._handle = new native.FileHandle(filePath);
    this.filesize = this._handle.filesize;
    this._arguments = new RuntimeArguments();
  }

  abstract get format(): DataFormat;
  abstract get positions(): Division;

  /**
   * Collection of spectra with lazy loading.
   */
  get spectra(): Spectra {
    if (this._spectra === null) {
      this._spectra = new Spectra(this, this.format, this.positions);
    }
    return this._spectra;
  }

  /**
   * Runtime configuration arguments for compression/decompression.
   */
  get arguments(): RuntimeArguments {
    return this._arguments;
  }

  /**
   * Get m/z binary array for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns m/z array
   */
  abstract getMzBinary(index: number): Float64Array | Float32Array;

  /**
   * Get intensity binary array for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns Intensity array
   */
  abstract getIntenBinary(index: number): Float64Array | Float32Array;

  /**
   * Get XML for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns XML string
   */
  abstract getXml(index: number): string;

  /**
   * Get file description with metadata.
   *
   * @returns Object containing file path, size, format, and positions
   */
  describe(): Record<string, unknown> {
    return {
      path: this.path,
      filesize: this.filesize,
      format: this.format,
      positions: this.positions,
    };
  }

  /**
   * Compress the file (implemented by MZMLFile).
   *
   * @param _output - Output file path
   * @returns Compressed file instance
   * @throws Error if not supported for this file type
   */
  compress(_output: string): BaseFile {
    throw new Error("Cannot compress this file type.");
  }

  /**
   * Decompress the file (implemented by MSZFile).
   *
   * @param _output - Output file path
   * @returns Decompressed file instance
   * @throws Error if not supported for this file type
   */
  decompress(_output: string): BaseFile {
    throw new Error("Cannot decompress this file type.");
  }

  /**
   * Extract filtered spectra to a new file.
   *
   * @param _output - Output file path
   * @param _options - Optional extraction filters
   * @returns Extracted file instance
   * @throws Error if not supported for this file type
   */
  extract(_output: string, _options?: ExtractOptions): BaseFile {
    throw new Error("Cannot extract from this file type.");
  }

  /**
   * Close the file and release resources.
   * Should be called when done with the file to avoid resource leaks.
   */
  close(): void {
    if (this._handle.isOpen) {
      this._handle.close();
    }
  }

  /**
   * Explicit resource management (TC39 proposal).
   * Allows using `using` keyword in TypeScript 5.2+.
   */
  [Symbol.dispose](): void {
    this.close();
  }
}
