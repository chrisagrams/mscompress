/**
 * Writer for v2 multi-file ("batch") MSZX archives.
 *
 * Wraps the shared C batch writer, so an archive written from Node is
 * byte-identical to one the CLI produces from the same inputs and settings.
 * Each entry streams straight into the archive file — no temp .msz staging —
 * which is why the output must be a seekable regular file.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import native, { type NativeBatchWriter } from "@/core/bindings.js";
import { MZMLFile } from "@/files/mzml-file.js";
import { RuntimeArguments } from "@/types/runtime-arguments.js";
import type { CompressArgs } from "@/types/types.js";

/** An mzML to add: a path, or an already-open MZMLFile. */
export type BatchInput = string | MZMLFile;

export interface AddAnnotationOptions {
  /** Reader-supported format tag, e.g. "percolator_tsv", "pepxml", "tsv". */
  format?: string;
  /** Records whether `data` is already zstd-compressed. */
  compressed?: boolean;
  /** Record/PSM count, if known. */
  numRecords?: number;
}

/**
 * Incremental writer for a v2 .mszx archive.
 *
 * The archive only becomes valid once {@link finish} succeeds — that writes
 * manifest.json and the end-of-archive marker. On any failure, call
 * {@link abort} to remove the partial file.
 *
 * @example
 * ```typescript
 * const writer = new MSZXBatchWriter("cohort.mszx", { threads: 8 });
 * try {
 *   const idx = await writer.add("run1.mzML");
 *   writer.addAnnotation(idx, pinBuffer, "annotations/run1.pin", {
 *     format: "percolator_tsv",
 *   });
 *   writer.finish();
 * } catch (err) {
 *   writer.abort();
 *   throw err;
 * }
 * ```
 */
export class MSZXBatchWriter {
  readonly path: string;

  private writer: NativeBatchWriter | null;
  private readonly args: RuntimeArguments;
  private readonly added: string[] = [];
  /** Entries append sequentially; overlapping adds would corrupt the archive. */
  private pending = false;

  /**
   * @param outputPath - Destination .mszx. Must be a seekable regular file.
   * @param options - Compression settings; omitted fields keep their defaults.
   */
  constructor(outputPath: string, options?: CompressArgs) {
    this.path = path.resolve(outputPath);
    this.args = new RuntimeArguments();
    if (options) {
      Object.assign(this.args, options);
    }
    this.writer = new native.BatchWriter(this.path);
  }

  private ensureOpen(): NativeBatchWriter {
    if (!this.writer) {
      throw new Error("MSZXBatchWriter: writer is finished or aborted");
    }
    return this.writer;
  }

  /**
   * Compress one mzML into the archive as a new entry.
   *
   * @param source - Path to an mzML, or an open MZMLFile.
   * @param entryName - Entry name inside the archive. Defaults to
   *   `<basename minus .mzML>.msz`; collisions get `__2`, `__3`, ... suffixes.
   *   Naming is handled by the C writer so the CLI and every binding agree.
   * @returns Index of the new entry, for {@link addAnnotation}/{@link setJoinKey}.
   */
  add(source: BatchInput, entryName?: string): Promise<number> {
    const writer = this.ensureOpen();
    if (this.pending) {
      // Thrown synchronously (not returned as a rejection) so misuse surfaces
      // at the call site. The native side guards this too, as a backstop.
      throw new Error(
        "MSZXBatchWriter: an add() is already in flight; entries are appended " +
          "sequentially and must not overlap"
      );
    }
    const sourcePath = typeof source === "string" ? source : source.path;
    this.pending = true;
    return writer
      .add(sourcePath, entryName ?? null, this.args.toNative())
      .then((index) => {
        this.added.push(sourcePath);
        return index;
      })
      .finally(() => {
        this.pending = false;
      });
  }

  /**
   * Attach an annotation file to a spectra entry, as its own archive member.
   *
   * Compression is the caller's job — pass already-compressed bytes and set
   * `compressed: true`. (`zstdCompress` is exported for that.)
   */
  addAnnotation(
    entryIndex: number,
    data: Buffer,
    name: string,
    options?: AddAnnotationOptions
  ): void {
    this.ensureOpen().addAnnotation(
      entryIndex,
      data,
      name,
      options?.format ?? "tsv",
      options?.compressed ?? false,
      options?.numRecords
    );
  }

  /** Set the column used to join annotations to spectra for one entry. */
  setJoinKey(entryIndex: number, joinKey: string): void {
    this.ensureOpen().setJoinKey(entryIndex, joinKey);
  }

  /** Set an archive-level free-text description. */
  setDescription(description: string): void {
    this.ensureOpen().setDescription(description);
  }

  /** Set the archive-level `extra` metadata object. */
  setExtra(extra: Record<string, unknown>): void {
    this.ensureOpen().setExtraJson(JSON.stringify(extra));
  }

  /** Source paths added so far, in archive order. */
  get entries(): string[] {
    return [...this.added];
  }

  get length(): number {
    return this.added.length;
  }

  /**
   * Write manifest.json and the end-of-archive marker, then close.
   *
   * On failure the partial archive is removed. The writer is unusable
   * afterwards either way.
   */
  finish(): void {
    const writer = this.ensureOpen();
    this.writer = null; // freed natively by finish(), success or not
    writer.finish();
  }

  /** Discard the archive, removing the partial file. Idempotent. */
  abort(): void {
    if (this.writer) {
      const writer = this.writer;
      this.writer = null;
      writer.abort();
    }
  }

  [Symbol.dispose](): void {
    this.abort();
  }
}

export interface CompressBatchOptions extends CompressArgs {
  /** Descend into subdirectories for directory inputs. */
  recursive?: boolean;
  /** Archive-level free-text description. */
  description?: string;
  /** Archive-level metadata, embedded in the manifest. */
  extra?: Record<string, unknown>;
  /** Called before each entry is compressed. */
  onProgress?: (index: number, total: number, path: string) => void;
}

/**
 * Expand paths and directories into a sorted list of mzML files.
 *
 * Ordering matches the CLI's — by basename, then by full path — so archives
 * built from the same set are deterministic. Directories contribute only
 * `*.mzML` (case-insensitive); explicitly named files are taken as-is.
 */
export function resolveMzmlInputs(
  inputs: string | string[],
  recursive = false
): string[] {
  const tokens = typeof inputs === "string" ? [inputs] : inputs;
  const found: string[] = [];

  const walk = (dir: string): void => {
    for (const dirent of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, dirent.name);
      if (dirent.isDirectory()) {
        if (recursive) walk(full);
      } else if (dirent.isFile() && dirent.name.toLowerCase().endsWith(".mzml")) {
        found.push(full);
      }
    }
  };

  for (const token of tokens) {
    if (!fs.existsSync(token)) {
      console.warn(`No input matched: ${token}`);
      continue;
    }
    if (fs.statSync(token).isDirectory()) {
      walk(token);
    } else {
      found.push(token);
    }
  }

  const unique = [...new Set(found.map((p) => path.resolve(p)))];
  return unique.sort((a, b) => {
    const byBase = path.basename(a).localeCompare(path.basename(b));
    return byBase !== 0 ? byBase : a.localeCompare(b);
  });
}

/**
 * Compress many mzML into one v2 .mszx archive.
 *
 * @param inputs - A path, directory, or array of either.
 * @param output - Destination .mszx.
 * @returns The written archive path.
 * @throws Error if no input matched, or an input could not be compressed.
 */
export async function compressBatch(
  inputs: string | string[],
  output: string,
  options?: CompressBatchOptions
): Promise<string> {
  const files = resolveMzmlInputs(inputs, options?.recursive ?? false);
  if (files.length === 0) {
    throw new Error("No input mzML files matched; nothing to do.");
  }

  const { recursive, description, extra, onProgress, ...compressArgs } = options ?? {};
  void recursive;

  const writer = new MSZXBatchWriter(output, compressArgs);
  try {
    if (description !== undefined) writer.setDescription(description);
    if (extra !== undefined) writer.setExtra(extra);
    for (let i = 0; i < files.length; i++) {
      onProgress?.(i, files.length, files[i]);
      await writer.add(files[i]);
    }
    writer.finish();
  } catch (err) {
    writer.abort();
    throw err;
  }
  return path.resolve(output);
}
