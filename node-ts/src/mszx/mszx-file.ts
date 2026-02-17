/**
 * MSZX bundled archive file handler
 */

import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import * as tar from 'tar';
import { MSZFile } from '../files/msz-file.js';
import { MSZXManifest, AnnotationEntry } from './mszx-manifest.js';
import { DataFormat } from '../types/data-format.js';
import { Spectra } from '../spectrum/spectra.js';
import { Division } from '../types/division.js';
import { RuntimeArguments } from '../types/runtime-arguments.js';
import type { ExtractOptions } from '../types/types.js';

/**
 * Handler for MSZX bundled archive files.
 *
 * Provides access to spectra via the same interface as MSZFile,
 * plus access to bundled annotation files and metadata.
 *
 * @example
 * ```typescript
 * import { MSZXFile } from 'mscompress';
 *
 * const mszx = MSZXFile.open('sample.mszx');
 * try {
 *   console.log(`Spectra: ${mszx.spectra.length}`);
 *
 *   // Access spectra
 *   for (const spectrum of mszx.spectra.slice(0, 10)) {
 *     console.log(spectrum.scan);
 *   }
 *
 *   // Access manifest
 *   console.log(mszx.manifest.description);
 * } finally {
 *   mszx.close();
 * }
 * ```
 */
export class MSZXFile {
  private archivePath: string;
  private manifestData: MSZXManifest;
  private mszFile: MSZFile;
  private tempDir: string;
  private closed: boolean = false;
  private annotationFiles: Map<string, Buffer> = new Map();

  private constructor(
    archivePath: string,
    manifest: MSZXManifest,
    mszFile: MSZFile,
    tempDir: string,
    annotationFiles: Map<string, Buffer>
  ) {
    this.archivePath = archivePath;
    this.manifestData = manifest;
    this.mszFile = mszFile;
    this.tempDir = tempDir;
    this.annotationFiles = annotationFiles;
  }

  /**
   * Open an MSZX archive for reading.
   *
   * @param filePath - Path to the .mszx file
   * @returns MSZXFile instance
   * @throws Error if the archive doesn't exist or is invalid
   */
  static open(filePath: string): MSZXFile {
    if (!fs.existsSync(filePath)) {
      throw new Error(`MSZX file not found: ${filePath}`);
    }

    // Create temp directory for extraction
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mszx-'));

    try {
      // Extract archive to temp directory
      tar.extract({
        file: filePath,
        cwd: tempDir,
        sync: true,
      });

      // Read manifest
      const manifestPath = path.join(tempDir, 'manifest.json');
      if (!fs.existsSync(manifestPath)) {
        throw new Error('Invalid MSZX archive: missing manifest.json');
      }

      const manifestJson = fs.readFileSync(manifestPath, 'utf-8');
      const manifest = MSZXManifest.parse(manifestJson);

      // Open MSZ file
      const mszPath = path.join(tempDir, manifest.spectra_file);
      if (!fs.existsSync(mszPath)) {
        throw new Error(`Invalid MSZX archive: missing spectra file ${manifest.spectra_file}`);
      }

      const mszFile = new MSZFile(mszPath);

      // Read annotation files into memory
      const annotationFiles = new Map<string, Buffer>();
      for (const entry of manifest.annotations) {
        const annotationPath = path.join(tempDir, entry.filename);
        if (fs.existsSync(annotationPath)) {
          const content = fs.readFileSync(annotationPath);
          annotationFiles.set(entry.filename, content);
        } else {
          console.warn(`Warning: Annotation file '${entry.filename}' not found in archive`);
        }
      }

      return new MSZXFile(filePath, manifest, mszFile, tempDir, annotationFiles);
    } catch (error) {
      // Clean up temp dir on failure
      try {
        fs.rmSync(tempDir, { recursive: true, force: true });
      } catch {}
      throw error;
    }
  }

  /**
   * Close the archive and clean up temporary files.
   */
  close(): void {
    if (!this.closed) {
      this.mszFile.close();
      try {
        fs.rmSync(this.tempDir, { recursive: true, force: true });
      } catch (error) {
        console.warn(`Warning: Failed to clean up temp directory: ${error}`);
      }
      this.closed = true;
    }
  }

  /**
   * Path to the MSZX archive.
   */
  get archive_path(): string {
    return this.archivePath;
  }

  /**
   * The archive manifest.
   */
  get manifest(): MSZXManifest {
    return this.manifestData;
  }

  /**
   * List of annotation entries in the archive.
   */
  get annotation_files(): AnnotationEntry[] {
    return this.manifestData.annotations;
  }

  /**
   * Get the raw content of an annotation file.
   *
   * @param filename - Name of the annotation file
   * @returns Buffer containing the file content
   * @throws Error if the file is not in the archive
   */
  getAnnotationFile(filename: string): Buffer {
    const content = this.annotationFiles.get(filename);
    if (!content) {
      throw new Error(`Annotation file not found: ${filename}`);
    }
    return content;
  }

  /**
   * Get annotation files by format.
   *
   * @param format - The annotation format to filter by
   * @returns Array of filenames matching the format
   */
  getAnnotationFilesByFormat(format: string): string[] {
    return this.manifestData.annotations
      .filter((entry) => entry.format === format)
      .map((entry) => entry.filename);
  }

  // Proxy properties to underlying MSZ file

  /**
   * Path to the underlying MSZ file.
   */
  get path(): string {
    return this.mszFile.path;
  }

  /**
   * Size of the underlying MSZ file.
   */
  get filesize(): number {
    return this.mszFile.filesize;
  }

  /**
   * Data format information.
   */
  get format(): DataFormat {
    return this.mszFile.format;
  }

  /**
   * Collection of spectra with lazy loading.
   */
  get spectra(): Spectra {
    return this.mszFile.spectra;
  }

  /**
   * Position information for data blocks.
   */
  get positions(): Division {
    return this.mszFile.positions;
  }

  /**
   * Runtime configuration arguments.
   */
  get arguments(): RuntimeArguments {
    return this.mszFile.arguments;
  }

  /**
   * Extract m/z binary array for a spectrum at the given index.
   *
   * @param index - Zero-based spectrum index
   * @returns m/z array (Float32Array or Float64Array)
   */
  getMzBinary(index: number): Float32Array | Float64Array {
    return this.mszFile.getMzBinary(index);
  }

  /**
   * Extract intensity binary array for a spectrum at the given index.
   *
   * @param index - Zero-based spectrum index
   * @returns Intensity array (Float32Array or Float64Array)
   */
  getIntenBinary(index: number): Float32Array | Float64Array {
    return this.mszFile.getIntenBinary(index);
  }

  /**
   * Extract XML string for a spectrum at the given index.
   *
   * @param index - Zero-based spectrum index
   * @returns XML string representation of the spectrum
   */
  getXml(index: number): string {
    return this.mszFile.getXml(index);
  }

  /**
   * Get description of the file.
   *
   * @returns Object containing file metadata and archive information
   */
  describe(): Record<string, any> {
    const desc = this.mszFile.describe();
    desc.archive = {
      path: this.archivePath,
      annotations: this.manifestData.annotations,
    };
    return desc;
  }

  /**
   * Decompress the MSZ file to mzML format.
   *
   * @param output - Output file path for decompressed mzML
   */
  decompress(output: string): void {
    this.mszFile.decompress(output);
  }

  /**
   * Extract a subset of spectra to a new MSZ file.
   *
   * Note: This extracts only the MSZ portion, not a full MSZX archive.
   * For MSZX extraction with annotations, use extractMSZX.
   *
   * @param output - Output file path
   * @param options - Optional extraction filters (indices, scanNumbers, msLevel)
   */
  extract(output: string, options?: ExtractOptions): void {
    this.mszFile.extract(output, options);
  }

  /**
   * Extract a subset of spectra and annotations to a new MSZX archive.
   *
   * @param output - Path to the output .mszx file
   * @param options - Extraction options
   * @returns New MSZXFile instance for the created archive
   */
  async extractMSZX(output: string, options?: ExtractOptions): Promise<MSZXFile> {
    // Create a temporary MSZ file with extracted spectra
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mszx-extract-'));
    const tempMszPath = path.join(tempDir, 'temp.msz');

    try {
      // Extract MSZ
      this.mszFile.extract(tempMszPath, options);
      const extractedMsz = new MSZFile(tempMszPath);

      // Create new MSZX archive using builder
      const { MSZXBuilder } = await import('./mszx-builder.js');
      const builder = new MSZXBuilder(extractedMsz);

      if (this.manifestData.description) {
        builder.setDescription(this.manifestData.description);
      }

      // Copy extra metadata
      for (const [key, value] of Object.entries(this.manifestData.extra)) {
        builder.setExtra(key, value);
      }

      // For now, we don't filter annotations - just include them all
      // In a full implementation, we would filter based on scan numbers
      // TODO: Implement annotation filtering

      const outputPath = await builder.save(output);
      extractedMsz.close();

      return MSZXFile.open(outputPath);
    } finally {
      // Clean up temp directory
      try {
        fs.rmSync(tempDir, { recursive: true, force: true });
      } catch {}
    }
  }
}
