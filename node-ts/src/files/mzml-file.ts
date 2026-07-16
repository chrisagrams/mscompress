import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { PassThrough } from "node:stream";
import native from "@/core/bindings.js";
import { BaseFile } from "@/files/base-file.js";
import { DataFormat } from "@/types/data-format.js";
import { Division } from "@/types/division.js";
import { createFile, registerFileFactory } from "@/files/file-registry.js";
import type { CompressArgs, ExtractOptions, RuntimeArgumentsNative } from "@/types/types.js";

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
   * thread and streams straight into an OS pipe that this Readable drains, so
   * large files stream incrementally rather than being buffered in memory.
   *
   * Native compression failures surface as an `'error'` event on the returned
   * stream (never a thrown exception or a hang).
   *
   * ### Per-platform strategy
   * - **POSIX (Linux/macOS):** true pipe streaming. Compression runs on a
   *   background thread writing into an OS `pipe(2)`; this Readable drains the
   *   read end. No temp file ever touches disk.
   * - **Windows:** the raw-fd-over-pipe handoff to Node's `fs.createReadStream`
   *   is not portable — a `_pipe()` read fd fed to libuv crashes the worker with
   *   an access violation. So on `win32` we compress to a temp file, stream it
   *   back, and unlink it once the read handle is closed (unlink-while-open is
   *   itself an EPERM footgun on Windows). Bytes are byte-for-byte identical to
   *   the POSIX path; only the transport differs.
   *
   * @param options - Optional `chunkSize` (read highWaterMark, default 1 MiB)
   *                  plus any {@link CompressArgs} overrides. Omitted compression
   *                  args fall back to this file's current `arguments`.
   * @returns A Readable stream of MSZ bytes.
   */
  compressStream(options?: { chunkSize?: number } & CompressArgs): NodeJS.ReadableStream {
    const { chunkSize, ...overrides } = options ?? {};
    const highWaterMark = chunkSize ?? 1024 * 1024;

    // Build effective compression args: start from this file's arguments and
    // apply any per-call overrides (same plumbing compress() uses).
    const args = this._arguments.toNative();
    for (const [key, value] of Object.entries(overrides)) {
      if (value !== undefined) {
        (args as unknown as Record<string, unknown>)[key] = value;
      }
    }

    return process.platform === "win32"
      ? this.compressStreamViaTempFile(args, highWaterMark)
      : this.compressStreamViaPipe(args, highWaterMark);
  }

  /**
   * POSIX true-pipe implementation of {@link MZMLFile.compressStream}. Runs
   * compression on a background thread writing into an OS pipe and drains the
   * read end — no temp file. Not used on Windows (see compressStream docs).
   */
  private compressStreamViaPipe(
    args: RuntimeArgumentsNative,
    highWaterMark: number
  ): NodeJS.ReadableStream {
    // A pass-through the caller consumes. We route the pipe's read end into it
    // so that any setup failure (closed handle, prepareDivisions error) or
    // native compression failure surfaces as a single 'error' event here,
    // rather than a synchronous throw.
    const output = new PassThrough({ highWaterMark });

    try {
      // Mirror compress(): prepare divisions and capture the resolved blocksize.
      const divResult = native.prepareDivisions(this._handle, args);
      args.blocksize = divResult.blocksize;
      this._arguments.blocksize = divResult.blocksize;

      const { readFd, done } = native.compressMzmlStream(this._handle, args);

      // Wrap the pipe's read end. autoClose closes read_fd exactly once on
      // 'end'/'error'/'close'. Blocking reads happen on the libuv threadpool,
      // so the event loop stays free.
      const source = fs.createReadStream("", { fd: readFd, highWaterMark, autoClose: true });

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
   * Windows implementation of {@link MZMLFile.compressStream}. Compresses to a
   * temp file (native.compressMzml opens/writes/closes its own fd — no lingering
   * handle), then streams the file back and removes it once the read handle is
   * fully closed. Produces the same bytes as the POSIX pipe path.
   */
  private compressStreamViaTempFile(
    args: RuntimeArgumentsNative,
    highWaterMark: number
  ): NodeJS.ReadableStream {
    const output = new PassThrough({ highWaterMark });
    let tmpDir: string | undefined;

    // Remove the temp dir. MUST run only after the read handle is closed —
    // unlinking a file with an open handle throws EPERM on Windows. force +
    // retries ride out any residual transient lock.
    const cleanup = (): void => {
      if (!tmpDir) return;
      const dir = tmpDir;
      tmpDir = undefined;
      try {
        fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
      } catch {
        // best-effort; a leaked temp file must never crash the stream
      }
    };

    try {
      // Mirror compress(): prepare divisions and capture the resolved blocksize.
      const divResult = native.prepareDivisions(this._handle, args);
      args.blocksize = divResult.blocksize;
      this._arguments.blocksize = divResult.blocksize;

      tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-stream-"));
      const tmpPath = path.join(tmpDir, "stream.msz");

      // Blocking compress; the native side owns and closes the output fd.
      native.compressMzml(this._handle, tmpPath, args);

      // autoClose closes the read fd exactly once on end/error/close. Only then
      // is it safe to unlink on Windows, so cleanup hangs off 'close'.
      const source = fs.createReadStream(tmpPath, { highWaterMark });
      source.on("error", (err) => {
        cleanup();
        output.destroy(err);
      });
      source.on("close", cleanup);
      source.pipe(output);
    } catch (err) {
      cleanup();
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
