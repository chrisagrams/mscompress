/**
 * Builder for creating MSZX archives from MSZ files
 */

import * as fs from "fs";
import * as path from "path";
import * as os from "os";
import * as tar from "tar";
import native from "@/core/bindings.js";
import { MSZFile } from "@/files/msz-file.js";
import { MSZXManifest, AnnotationEntry } from "@/mszx/mszx-manifest.js";
import { MzTabReader, writeMzTab } from "@/annotations/mztab.js";
import type { AnnotationFormat, PSM, SearchInfo } from "@/annotations/psm.js";
import { convertToMzTab, mzTabMemberName } from "@/annotations/reader.js";

/**
 * Builder for creating MSZX archives from MSZ files and annotations.
 *
 * @example
 * ```typescript
 * import { read, MSZXBuilder } from 'mscompress';
 *
 * const msz = read('sample.msz') as MSZFile;
 * const builder = new MSZXBuilder(msz);
 * builder.setDescription('Proteomics dataset with annotations');
 * await builder.save('sample.mszx');
 * ```
 */
export class MSZXBuilder {
  private msz: MSZFile;
  private mszPath: string;
  private annotations: Array<{ data: Buffer; entry: AnnotationEntry }> = [];
  private description?: string;
  private sourceName: string;
  private joinKey: string = "spectra_ref";
  private extra: Record<string, unknown> = {};

  /**
   * Create a new MSZX archive builder.
   *
   * @param msz - MSZFile instance to bundle in the archive
   * @param sourceName - Optional source file name for provenance (defaults to MSZ filename)
   * @throws TypeError if msz is not an MSZFile instance
   */
  constructor(msz: MSZFile, sourceName?: string) {
    if (!(msz instanceof MSZFile)) {
      throw new TypeError("msz must be an MSZFile instance");
    }
    this.msz = msz;
    this.mszPath = msz.path;
    this.sourceName = sourceName || path.basename(this.mszPath);
  }

  /**
   * Add search results to the archive.
   *
   * The file is converted to mzTab (the only format archives store) and the
   * original is not kept. The member is named `<basename>.mztab` (an mzTab
   * input keeps its name), plus `.zst` when `compressed` (default `true`,
   * matching the Python builder).
   *
   * @param filePath - Path to an mzTab, pepXML or Percolator TSV/PIN file
   *   (optionally zstd-compressed with a `.zst` suffix).
   * @param options - Annotation options. `format` is a hint for the *input*
   *   format, used instead of auto-detection; the stored format is always mzTab.
   * @returns This builder for method chaining
   * @throws Error if the file is missing or its format cannot be determined.
   */
  addAnnotations(
    filePath: string,
    options?: {
      description?: string;
      format?: AnnotationFormat;
      compressed?: boolean;
    }
  ): this {
    if (!fs.existsSync(filePath)) {
      throw new Error(`Annotation file not found: ${filePath}`);
    }
    const conversion = convertToMzTab(fs.readFileSync(filePath), filePath, {
      format: options?.format,
      msRunLocation: path.basename(this.mszPath),
    });
    const compressed = options?.compressed ?? true;
    return this.addMzTab(conversion.text, {
      filename: mzTabMemberName(filePath, compressed, conversion.inputFormat === "mztab"),
      description: options?.description,
      compressed,
      numRecords: conversion.numRecords,
    });
  }

  /**
   * Add in-memory PSMs to the archive, written as mzTab.
   *
   * @param psms - PSMs to store.
   * @param options - `filename` defaults to `"psms.mztab"` (`.mztab` is appended
   *   if missing, `.zst` when `compressed`, which defaults to `true`).
   * @returns This builder for method chaining
   */
  addPSMs(
    psms: Iterable<PSM>,
    options?: {
      filename?: string;
      searchInfo?: SearchInfo;
      description?: string;
      compressed?: boolean;
    }
  ): this {
    const list = [...psms];
    const text = writeMzTab(list, {
      searchInfo: options?.searchInfo,
      msRunLocation: path.basename(this.mszPath),
    });
    const compressed = options?.compressed ?? true;
    return this.addMzTab(text, {
      filename: mzTabMemberName(options?.filename ?? "psms.mztab", compressed),
      description: options?.description,
      compressed,
      numRecords: list.length,
    });
  }

  /**
   * Add an mzTab document verbatim, under an explicit member name.
   *
   * @param content - mzTab text or bytes.
   * @param options - `filename` is the exact member name; `numRecords` is
   *   counted by parsing `content` when omitted.
   * @returns This builder for method chaining
   * @throws Error if a member with the same name was already added.
   */
  addMzTab(
    content: string | Buffer,
    options: {
      filename: string;
      description?: string;
      compressed?: boolean;
      numRecords?: number;
    }
  ): this {
    const filename = options.filename;
    if (this.annotations.some((a) => a.entry.filename === filename)) {
      throw new Error(`Duplicate annotation member name: ${filename}`);
    }
    const raw = typeof content === "string" ? Buffer.from(content, "utf-8") : content;
    const compressed = options.compressed ?? true;
    const entry: AnnotationEntry = {
      filename,
      format: "mztab",
      compressed,
      description: options.description,
      num_records: options.numRecords ?? new MzTabReader(raw).length,
    };
    this.annotations.push({ data: compressed ? native.zstdCompress(raw) : raw, entry });
    return this;
  }

  /**
   * Set the archive description.
   *
   * @param description - Human-readable description of the archive
   * @returns This builder for method chaining
   */
  setDescription(description: string): this {
    this.description = description;
    return this;
  }

  /**
   * Set the key used to join spectra with annotations.
   *
   * @param joinKey - Join key (default 'spectra_ref', matching the mzTab
   *   `spectra_ref` column; e.g. 'scan_number')
   * @returns This builder for method chaining
   */
  setJoinKey(joinKey: string): this {
    this.joinKey = joinKey;
    return this;
  }

  /**
   * Add extra metadata to the manifest.
   *
   * @param key - Metadata key
   * @param value - Metadata value
   * @returns This builder for method chaining
   */
  setExtra(key: string, value: unknown): this {
    this.extra[key] = value;
    return this;
  }

  /**
   * Build the manifest from current state.
   *
   * @returns MSZXManifest instance with current builder configuration
   * @private
   */
  private buildManifest(): MSZXManifest {
    return new MSZXManifest({
      spectra_file: path.basename(this.mszPath),
      num_spectra: this.msz.spectra.length,
      annotations: this.annotations.map((a) => a.entry),
      join_key: this.joinKey,
      description: this.description,
      source_file: this.sourceName,
      extra: this.extra,
    });
  }

  /** Write the (already converted, possibly compressed) annotation members into `dir`. */
  private stageAnnotations(dir: string): void {
    for (const { data, entry } of this.annotations) {
      fs.writeFileSync(path.join(dir, entry.filename), data);
    }
  }

  /**
   * Save the MSZX archive to disk.
   *
   * @param outputPath - Output file path (should end with .mszx)
   * @returns Promise resolving to the path of the created archive
   */
  async save(outputPath: string): Promise<string> {
    let output = outputPath;
    if (!path.extname(output)) {
      output = output + ".mszx";
    }

    const manifest = this.buildManifest();

    // Create temporary directory for staging files
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-build-"));

    try {
      // Write manifest to temp directory
      const manifestPath = path.join(tempDir, "manifest.json");
      fs.writeFileSync(manifestPath, manifest.toString());

      // Copy MSZ file to temp directory
      const tempMszPath = path.join(tempDir, manifest.spectra_file);
      fs.copyFileSync(this.mszPath, tempMszPath);

      this.stageAnnotations(tempDir);

      // Create tar archive from temp directory
      await tar.create(
        {
          file: output,
          cwd: tempDir,
          gzip: false,
        },
        fs.readdirSync(tempDir)
      );

      return output;
    } finally {
      // Clean up temp directory
      fs.rmSync(tempDir, { recursive: true, force: true });
    }
  }

  /**
   * Synchronous variant of `save()`. Used by `MSZXFile.extract` for
   * `.mszx` output, where the surrounding API is sync (matching
   * MSZFile.extract). Behaves identically to `save()` apart from running
   * tar creation in sync mode.
   *
   * @param outputPath - Output file path (should end with .mszx).
   * @returns Path of the created archive.
   */
  saveSync(outputPath: string): string {
    let output = outputPath;
    if (!path.extname(output)) {
      output = output + ".mszx";
    }

    const manifest = this.buildManifest();
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-build-"));

    try {
      const manifestPath = path.join(tempDir, "manifest.json");
      fs.writeFileSync(manifestPath, manifest.toString());

      const tempMszPath = path.join(tempDir, manifest.spectra_file);
      fs.copyFileSync(this.mszPath, tempMszPath);

      this.stageAnnotations(tempDir);

      tar.create(
        {
          file: output,
          cwd: tempDir,
          gzip: false,
          sync: true,
        },
        fs.readdirSync(tempDir)
      );

      return output;
    } finally {
      fs.rmSync(tempDir, { recursive: true, force: true });
    }
  }
}
