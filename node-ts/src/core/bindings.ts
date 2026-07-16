import { createRequire } from "node:module";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type {
  DataFormatNative,
  DivisionNative,
  FooterNative,
  RuntimeArgumentsNative,
  ExtractOptions,
} from "../types/types.js";

// Load the native addon (.node files must be loaded via require())
const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const require = createRequire(import.meta.url);
const addonPath = path.join(__dirname, "..", "..", "build", "Release", "mscompress.node");
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
const addon = require(addonPath);

/** RAII file handle wrapping fd + mmap + filesize */
export interface NativeFileHandle {
  close(): void;
  readonly filesize: number;
  readonly filetype: number;
  readonly isOpen: boolean;
}

export interface NativeFileHandleConstructor {
  new (path: string): NativeFileHandle;
}

export interface MszDivisionsResult {
  positions: DivisionNative;
  footer: FooterNative;
}

export interface PrepareDivisionsResult {
  nDivisions: number;
  blocksize: number;
}

export interface NativeBindings {
  // FileHandle class
  FileHandle: NativeFileHandleConstructor;

  // File operations
  getFilesize(path: string): number;
  getNumThreads(): number;
  openOutputFile(path: string): number;
  closeOutputFile(fd: number): void;
  getVersion(): string;

  // Format detection
  getDataFormat(handle: NativeFileHandle): DataFormatNative;
  setCompressRuntimeVars(handle: NativeFileHandle, args: RuntimeArgumentsNative): void;
  setDecompressRuntimeVars(handle: NativeFileHandle): void;

  // Division / position scanning
  scanMzml(handle: NativeFileHandle): DivisionNative;
  readMszDivisions(handle: NativeFileHandle): MszDivisionsResult;
  prepareDivisions(handle: NativeFileHandle, args: RuntimeArgumentsNative): PrepareDivisionsResult;

  // Spectrum access - mzML
  getMzBinaryMzml(
    handle: NativeFileHandle,
    index: number,
    blocksize: number
  ): Float64Array | Float32Array;
  getIntenBinaryMzml(
    handle: NativeFileHandle,
    index: number,
    blocksize: number
  ): Float64Array | Float32Array;
  getXmlMzml(handle: NativeFileHandle, index: number): string;

  // Spectrum access - MSZ
  getMzBinaryMsz(handle: NativeFileHandle, index: number): Float64Array | Float32Array;
  getIntenBinaryMsz(handle: NativeFileHandle, index: number): Float64Array | Float32Array;
  getXmlMsz(handle: NativeFileHandle, index: number): string;

  // Compression / decompression
  compressMzml(handle: NativeFileHandle, outputPath: string, args: RuntimeArgumentsNative): boolean;
  /**
   * Compress an mzML file to MSZ, streaming the output into an OS pipe instead
   * of a file. Returns the read-end fd (wrap in a Readable) plus a promise that
   * resolves when compression completes or rejects on failure. The write end is
   * always closed by the native side, so the reader observes EOF.
   */
  compressMzmlStream(
    handle: NativeFileHandle,
    args: RuntimeArgumentsNative
  ): { readFd: number; done: Promise<void> };
  decompressMsz(
    handle: NativeFileHandle,
    outputPath: string,
    args: RuntimeArgumentsNative
  ): boolean;

  // Extraction
  extractMzmlFiltered(
    handle: NativeFileHandle,
    outputPath: string,
    options: ExtractOptions
  ): boolean;
  extractMsz(handle: NativeFileHandle, outputPath: string, options: ExtractOptions): boolean;

  // Zstd utilities
  zstdCompress(data: Buffer, level?: number): Buffer;
  zstdDecompress(data: Buffer): Buffer;

  // MSZX archive helpers — open the embedded MSZ payload at its offset
  // inside a .mszx tar without extracting to a temp file.
  openMszFromArchive(archivePath: string, entryName: string): NativeFileHandle;
}

const native: NativeBindings = addon as NativeBindings;
export default native;
