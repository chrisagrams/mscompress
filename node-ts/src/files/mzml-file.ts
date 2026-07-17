import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { PassThrough } from "node:stream";
import native from "@/core/bindings.js";
import { BaseFile } from "@/files/base-file.js";
import { DataFormat } from "@/types/data-format.js";
import { Division } from "@/types/division.js";
import { createFile, registerFileFactory } from "@/files/file-registry.js";
import type { CompressArgs, ExtractOptions } from "@/types/types.js";

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
    if (index < 0 || index >= this.spectra.length) {
      throw new RangeError(`Spectrum index ${index} out of range [0, ${this.spectra.length})`);
    }
    return native.getMzBinaryMzml(this._handle, index, this._arguments.blocksize);
  }

  /**
   * Get intensity binary array for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns Intensity array
   */
  getIntenBinary(index: number): Float64Array | Float32Array {
    if (index < 0 || index >= this.spectra.length) {
      throw new RangeError(`Spectrum index ${index} out of range [0, ${this.spectra.length})`);
    }
    return native.getIntenBinaryMzml(this._handle, index, this._arguments.blocksize);
  }

  /**
   * Get XML for a spectrum.
   *
   * @param index - Zero-based spectrum index
   * @returns XML string
   */
  getXml(index: number): string {
    if (index < 0 || index >= this.spectra.length) {
      throw new RangeError(`Spectrum index ${index} out of range [0, ${this.spectra.length})`);
    }
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

    // Prepare divisions for compression and capture any updated settings
    const divResult = native.prepareDivisions(this._handle, this._arguments.toNative());
    this._arguments.blocksize = divResult.blocksize;

    // Run compression
    native.compressMzml(this._handle, outputPath, this._arguments.toNative());

    return createFile("msz", outputPath);
  }

  /**
   * Compress this mzML to MSZ format and expose the MSZ bytes as a Node
   * {@link https://nodejs.org/api/stream.html#class-streamreadable Readable}
   * stream — without ever writing a temporary `.msz` file to disk.
   *
   * The stream yields the exact same bytes that {@link MZMLFile.compress} would
   * write to a file for the same arguments. Compression runs on a background
   * thread that writes straight into an OS pipe; this Readable drains the read
   * end, so large files stream incrementally rather than being buffered in
   * memory. The identical code path runs on every platform (see below).
   *
   * Native compression failures surface as an `'error'` event on the returned
   * stream (never a thrown exception or a hang). End-of-stream is driven by the
   * native `done` signal.
   *
   * The pipe's read end is wrapped with {@link net.Socket} rather than
   * `fs.createReadStream`, because a Socket adopts the fd through a libuv pipe
   * handle (`uv_pipe_open`) on every platform. That matters on Windows: the raw
   * CRT fd returned by `_pipe()` is not a libuv-pollable handle, so
   * `fs.createReadStream({ fd })` aborts the worker with an access violation,
   * whereas `net.Socket` converts it (via `_get_osfhandle`) into a real,
   * backpressure-aware pipe stream — exactly what POSIX gets from the fd. One
   * transport, one behavior, no temp file, no Windows special-case.
   *
   * @param options - Optional `chunkSize` (the read buffer highWaterMark,
   *                  default 1 MiB) plus any {@link CompressArgs} overrides.
   *                  Omitted compression args fall back to this file's current
   *                  `arguments`.
   * @returns A Readable stream of MSZ bytes.
   */
  compressStream(options?: { chunkSize?: number } & CompressArgs): NodeJS.ReadableStream {
    const { chunkSize, ...overrides } = options ?? {};

    // `chunkSize` is the read buffer's highWaterMark: the target number of MSZ
    // bytes buffered before backpressure pauses the producer. Default 1 MiB.
    const readBufferSize = chunkSize ?? 1024 * 1024;

    // Effective native args: this file's current settings with any provided
    // CompressArgs overrides applied on top (same result compress() produces,
    // but per-call and without mutating this._arguments). Only defined
    // overrides win; the typed key loop keeps this fully type-safe.
    const args = this._arguments.toNative();
    for (const key of Object.keys(overrides) as (keyof CompressArgs)[]) {
      const value = overrides[key];
      if (value !== undefined) {
        args[key] = value;
      }
    }

    // A pass-through the caller consumes. We route the pipe's read end into it
    // so that any setup failure (closed handle, prepareDivisions error) or
    // native compression failure surfaces as a single 'error' event here,
    // rather than a synchronous throw.
    const output = new PassThrough({ highWaterMark: readBufferSize });

    try {
      // Mirror compress(): prepare divisions and capture the resolved blocksize.
      const divResult = native.prepareDivisions(this._handle, args);
      args.blocksize = divResult.blocksize;
      this._arguments.blocksize = divResult.blocksize;

      const { readFd, done } = native.compressMzmlStream(this._handle, args);

      // Wrap the pipe's read end as a read-only Socket. The Socket owns the fd
      // and closes it once destroyed (on 'end'/'error'). Reads are non-blocking
      // via libuv, so the event loop stays free and backpressure flows through.
      const source = new net.Socket({ fd: readFd, readable: true, writable: false });

      source.on("error", (err) => {
        output.destroy(err);
      });
      source.pipe(output);

      // The reader hits a clean EOF whether compression succeeded or failed
      // (the native side always closes the write end). The `done` promise is
      // the authoritative success/failure signal, so a rejection must tear the
      // consumer's stream down with an error even if EOF already arrived.
      done.catch((err: unknown) => {
        const error = err instanceof Error ? err : new Error(String(err));
        source.destroy();
        if (!output.destroyed) {
          output.destroy(error);
        }
      });
    } catch (err) {
      // Synchronous setup failure (e.g. closed handle). Surface via the stream
      // on the next tick so listeners have a chance to attach.
      const error = err instanceof Error ? err : new Error(String(err));
      queueMicrotask(() => output.destroy(error));
    }

    return output;
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
        const tmpFile = this.extract(tmpMzml, options);
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
