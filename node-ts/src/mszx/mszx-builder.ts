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
  private annotations: Array<{ path: string; entry: AnnotationEntry }> = [];
  private description?: string;
  private sourceName: string;
  private joinKey: string = "scan_number";
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
   * Add annotations to the archive.
   *
   * @param filePath - Path to the annotation file
   * @param options - Annotation options
   * @returns This builder for method chaining
   */
  addAnnotations(
    filePath: string,
    options?: {
      description?: string;
      format?: "percolator_tsv" | "pepxml" | "tsv";
      compressed?: boolean;
    }
  ): this {
    if (!fs.existsSync(filePath)) {
      throw new Error(`Annotation file not found: ${filePath}`);
    }

    const compressed = options?.compressed ?? false;
    const filename = path.basename(filePath) + (compressed ? ".zst" : "");

    // Auto-detect format from extension if not provided
    let format = options?.format;
    if (!format) {
      const ext = path.extname(filePath).toLowerCase();
      if (ext === ".pin" || ext === ".tsv") {
        format = "percolator_tsv";
      } else if (ext === ".pepxml" || ext === ".xml") {
        format = "pepxml";
      } else {
        format = "tsv";
      }
    }

    // Count lines for num_records (simple implementation)
    const content = fs.readFileSync(filePath, "utf-8");
    const lines = content.split("\n").filter((line) => line.trim().length > 0);
    const num_records = Math.max(0, lines.length - 1); // Subtract header

    const entry: AnnotationEntry = {
      filename,
      format,
      compressed,
      description: options?.description,
      num_records,
    };

    this.annotations.push({ path: filePath, entry });
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
   * @param joinKey - Join key (e.g., 'scan_number', 'index')
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

      // Copy annotation files to temp directory
      for (const { path: annotationPath, entry } of this.annotations) {
        const tempAnnotationPath = path.join(tempDir, entry.filename);

        if (entry.compressed) {
          const data = fs.readFileSync(annotationPath);
          const compressed = native.zstdCompress(data);
          fs.writeFileSync(tempAnnotationPath, compressed);
        } else {
          fs.copyFileSync(annotationPath, tempAnnotationPath);
        }
      }

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
}
