import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { Readable } from "node:stream";
import native from "@/core/bindings.js";
import type { CompressStreamSession } from "@/core/bindings.js";
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
   * thread that writes into an OS pipe owned entirely by the native addon; this
   * Readable pulls compressed chunks from that pipe on demand, so large files
   * stream incrementally rather than being buffered in memory. One identical
   * code path runs on every platform.
   *
   * ### Why the pipe fd never crosses into JS
   * Earlier attempts handed the pipe's raw read fd to Node (`fs.createReadStream`
   * or `net.Socket({ fd })`). That works on POSIX but is fundamentally broken on
   * Windows: the fd from the CRT `_pipe()` is an anonymous pipe, and libuv's
   * `uv_guess_handle` returns `UNKNOWN` for it — so `net.Socket` throws
   * `ERR_INVALID_FD_TYPE` and `fs.createReadStream` faults the worker with an
   * access violation. There is no way to adopt an anonymous CRT pipe fd into
   * libuv on Windows. So the fd stays native-only: JS pulls bytes via an async
   * threadpool read (`compressMzmlStreamRead`), which is portable everywhere.
   *
   * Native compression failures surface as an `'error'` event on the returned
   * stream (never a thrown exception or a hang). End-of-stream is driven by the
   * native EOF signal (a `null` read result), at which point the background
   * thread is joined and its result checked.
   *
   * Backpressure is honored: the next native read is only issued from the
   * Readable's `_read()`, i.e. after the consumer has drained the previous
   * chunk, so at most one `chunkSize` buffer is in flight at a time.
   *
   * @param options - Optional `chunkSize` (bytes read per pull, default 1 MiB)
   *                  plus any {@link CompressArgs} overrides. Omitted compression
   *                  args fall back to this file's current `arguments`.
   * @returns A Readable stream of MSZ bytes.
   */
  compressStream(options?: { chunkSize?: number } & CompressArgs): NodeJS.ReadableStream {
    const { chunkSize, ...overrides } = options ?? {};

    // Bytes requested per native pull. Also used as the Readable highWaterMark
    // so the stream's buffering target matches the chunk size. Default 1 MiB.
    const readChunkSize = chunkSize ?? 1024 * 1024;

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

    let session: CompressStreamSession | undefined;
    let reading = false; // guard against overlapping native reads

    const output = new Readable({
      highWaterMark: readChunkSize,
      read() {
        // Pull exactly one chunk per _read() call. Node calls _read() again
        // when it wants more (i.e. after the consumer drains below the
        // highWaterMark), so backpressure is honored and at most one native
        // read is outstanding at a time (guarded by `reading`).
        if (reading || session === undefined) return;
        reading = true;
        native
          .compressMzmlStreamRead(session, readChunkSize)
          .then((chunk) => {
            reading = false;
            // null signals EOF (compression finished successfully); otherwise
            // push the chunk and wait for the next _read().
            this.push(chunk === null ? null : chunk);
          })
          .catch((err: unknown) => {
            reading = false;
            const error = err instanceof Error ? err : new Error(String(err));
            this.destroy(error);
          });
      },
    });

    try {
      // Mirror compress(): prepare divisions and capture the resolved blocksize.
      const divResult = native.prepareDivisions(this._handle, args);
      args.blocksize = divResult.blocksize;
      this._arguments.blocksize = divResult.blocksize;

      // Open the session (starts the background compression thread). The native
      // finalizer joins the thread and frees the pipe when `session` is GC'd.
      session = native.compressMzmlStreamOpen(this._handle, args);
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
