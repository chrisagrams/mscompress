/**
 * MSZX bundled archive file handler.
 *
 * MSZXFile extends MSZFile, so all spectrum-access machinery
 * (`spectra`, `positions`, `getMzBinary`, `getIntenBinary`, `getXml`,
 * `decompress`, `extract`) is inherited directly. Constructed via
 * `MSZXFile.open(...)`, which mmaps the embedded MSZ payload at its
 * byte offset inside the .mszx tar — no temp-dir extraction.
 */

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as tar from "tar";
import native from "@/core/bindings.js";
import type { BaseFile } from "@/files/base-file.js";
import { MSZFile } from "@/files/msz-file.js";
import { MSZXBuilder } from "@/mszx/mszx-builder.js";
import { MSZXManifest, AnnotationEntry } from "@/mszx/mszx-manifest.js";
import type { ExtractOptions } from "@/types/types.js";
import { MzTabReader, writeMzTab } from "@/annotations/mztab.js";
import type { PSM, PSMCollection } from "@/annotations/psm.js";
import { mzTabMemberName, readPSMs } from "@/annotations/reader.js";

interface CapturedTarEntries {
  manifest: MSZXManifest;
  annotations: Map<string, Buffer>;
}

/**
 * Walk a tar archive synchronously, capturing every regular-file entry's
 * payload into a Map keyed by entry name. Single pass — cheap because the
 * `tar` package's sync mode reads the whole file into memory anyway.
 */
function collectTarEntries(filePath: string): Map<string, Buffer> {
  const entries = new Map<string, Buffer>();
  tar.list({
    file: filePath,
    sync: true,
    onReadEntry: (entry) => {
      const chunks: Buffer[] = [];
      entry.on("data", (chunk: Buffer) => chunks.push(chunk));
      entry.on("end", () => {
        entries.set(entry.path, Buffer.concat(chunks));
      });
    },
  });
  return entries;
}

/**
 * Read manifest + annotation buffers without extracting to a temp dir.
 * Annotation entries listed by the manifest as `compressed: true` are
 * decompressed via the addon's zstd helper.
 */
function readManifestAndAnnotations(filePath: string): CapturedTarEntries {
  const entries = collectTarEntries(filePath);

  const manifestBuf = entries.get("manifest.json");
  if (!manifestBuf) {
    throw new Error("Invalid MSZX archive: missing manifest.json");
  }
  const manifest = MSZXManifest.parse(manifestBuf.toString("utf-8"));

  const annotations = new Map<string, Buffer>();
  for (const entry of manifest.annotations) {
    const raw = entries.get(entry.filename);
    if (!raw) {
      console.warn(`Warning: Annotation file '${entry.filename}' not found in archive`);
      continue;
    }
    annotations.set(entry.filename, entry.compressed ? native.zstdDecompress(raw) : raw);
  }

  return { manifest, annotations };
}

/**
 * Handler for MSZX bundled archive files.
 *
 * Inherits the full MSZFile API. `mszx instanceof MSZFile === true`.
 *
 * @example
 * ```typescript
 * import { MSZXFile } from "mscompress";
 *
 * const mszx = MSZXFile.open("sample.mszx");
 * try {
 *   console.log(`Spectra: ${mszx.spectra.length}`);
 *   const buf = mszx.getMzBinary(0);
 *   console.log(mszx.manifest.description);
 * } finally {
 *   mszx.close();
 * }
 * ```
 */
export class MSZXFile extends MSZFile {
  readonly archivePath: string;
  readonly manifest: MSZXManifest;
  /** Decompressed annotation member contents, keyed by member name. */
  readonly annotations: Map<string, Buffer>;

  /** Parsed annotation readers, filled lazily by {@link getPSMReader}. */
  private readonly readerCache = new Map<string, PSMCollection>();

  private constructor(
    archivePath: string,
    manifest: MSZXManifest,
    annotations: Map<string, Buffer>
  ) {
    const handle = native.openMszFromArchive(archivePath, manifest.spectra_file);
    super({ handle, path: archivePath });
    this.archivePath = archivePath;
    this.manifest = manifest;
    this.annotations = annotations;
  }

  /**
   * Open an MSZX archive for reading. Mmaps the embedded MSZ in place.
   *
   * @param filePath - Path to the .mszx file.
   * @returns MSZXFile instance.
   * @throws Error if the archive doesn't exist or is invalid.
   */
  static open(filePath: string): MSZXFile {
    if (!fs.existsSync(filePath)) {
      throw new Error(`MSZX file not found: ${filePath}`);
    }
    const { manifest, annotations } = readManifestAndAnnotations(filePath);
    return new MSZXFile(filePath, manifest, annotations);
  }

  /**
   * Path to the MSZX archive (alias matching the Python binding's snake_case
   * property; the inherited `path` returns the same value).
   */
  get archive_path(): string {
    return this.archivePath;
  }

  /**
   * List of annotation entries in the archive's manifest.
   */
  get annotation_files(): AnnotationEntry[] {
    return this.manifest.annotations;
  }

  /**
   * Get the raw (decompressed) content of an annotation file.
   *
   * @param filename - Name of the annotation file in the manifest.
   * @returns Buffer containing the file content.
   * @throws Error if the file is not in the archive.
   */
  getAnnotationFile(filename: string): Buffer {
    const content = this.annotations.get(filename);
    if (!content) {
      throw new Error(`Annotation file not found: ${filename}`);
    }
    return content;
  }

  /**
   * List annotation filenames matching a given format.
   *
   * @param format - The annotation format to filter by.
   * @returns Array of filenames matching the format.
   */
  getAnnotationFilesByFormat(format: string): string[] {
    return this.manifest.annotations
      .filter((entry) => entry.format === format)
      .map((entry) => entry.filename);
  }

  /**
   * Parsed PSMs of one annotation member. Parsed on first use and cached.
   *
   * mzTab members (everything written by current versions) come back as an
   * {@link MzTabReader}; members of older archives (pepXML, Percolator) are
   * parsed with their engine reader and expose the same query API.
   *
   * @param filename - Member name from the manifest.
   * @returns The PSM collection.
   * @throws Error if the member is missing or cannot be parsed.
   */
  getPSMReader(filename: string): PSMCollection {
    const cached = this.readerCache.get(filename);
    if (cached) return cached;
    const content = this.getAnnotationFile(filename);
    const entry = this.manifest.annotations.find((a) => a.filename === filename);
    const reader = readPSMs(content, filename, entry?.format);
    this.readerCache.set(filename, reader);
    return reader;
  }

  /**
   * Parsed PSM readers for every annotation member, keyed by member name, in
   * manifest order. Members that fail to parse are skipped with a warning.
   */
  get psmReaders(): Map<string, PSMCollection> {
    const readers = new Map<string, PSMCollection>();
    for (const entry of this.manifest.annotations) {
      if (!this.annotations.has(entry.filename)) continue;
      try {
        readers.set(entry.filename, this.getPSMReader(entry.filename));
      } catch (err) {
        console.warn(
          `Warning: could not parse annotation '${entry.filename}': ${(err as Error).message}`
        );
      }
    }
    return readers;
  }

  /**
   * The primary annotation reader: the first mzTab member, else the first
   * parsable legacy member, else null.
   */
  get annotationReader(): PSMCollection | null {
    const readers = this.psmReaders;
    for (const reader of readers.values()) {
      if (reader instanceof MzTabReader) return reader;
    }
    const first = readers.values().next();
    return first.done ? null : first.value;
  }

  /** PSMs of the primary annotation (empty if the archive has none). */
  get psms(): PSM[] {
    return this.annotationReader?.psms ?? [];
  }

  /**
   * Decompress to mzML.
   *
   * If `output` has no extension (treated as a directory), writes
   * `<archive_stem>.mzML` plus all cached annotation buffers as siblings
   * (with any trailing `.zst` suffix stripped, since annotations are
   * stored decompressed in memory). Otherwise, behaves like the inherited
   * MSZFile.decompress and writes the mzML to the given file path.
   *
   * @param output - Output file path or directory.
   * @returns BaseFile for the produced mzML.
   */
  decompress(output: string): BaseFile {
    const isDir = path.extname(output) === "";
    if (!isDir) {
      return super.decompress(output);
    }

    fs.mkdirSync(output, { recursive: true });
    const stem = path.basename(this.archivePath, path.extname(this.archivePath));
    const mzmlPath = path.join(output, `${stem}.mzML`);
    const result = super.decompress(mzmlPath);

    for (const [name, buf] of this.annotations) {
      const outName = name.endsWith(".zst") ? name.slice(0, -4) : name;
      fs.writeFileSync(path.join(output, outName), buf);
    }
    return result;
  }

  /**
   * Extract a subset of spectra to a new file. For .mzML / .msz output,
   * inherits the standard MSZFile.extract behavior. For .mszx output,
   * builds a filtered MSZX archive containing the selected spectra plus
   * every annotation, filtered to the PSMs of the extracted scans and stored
   * as mzTab (legacy pepXML/Percolator members are converted).
   *
   * @param output - Output file path. Extension determines format.
   * @param options - Optional filters (indices, scanNumbers, msLevel).
   * @returns BaseFile for the produced output. For .mszx output the
   *          returned value is a fresh MSZXFile.
   */
  extract(output: string, options?: ExtractOptions): BaseFile {
    if (path.extname(output).toLowerCase() !== ".mszx") {
      return super.extract(output, options);
    }
    return this.extractToMszx(output, options);
  }

  /**
   * Build a filtered MSZX archive synchronously: extract a temp .msz with
   * the selected spectra, then bundle it together with the original
   * annotation buffers via MSZXBuilder.save.
   */
  private extractToMszx(output: string, options?: ExtractOptions): MSZXFile {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-extract-"));
    const tmpMszPath = path.join(tmpDir, "temp.msz");
    let extractedMsz: MSZFile | null = null;

    try {
      // Use parent's prototype to avoid recursing through this override.
      MSZFile.prototype.extract.call(this, tmpMszPath, options);
      extractedMsz = new MSZFile(tmpMszPath);

      const builder = new MSZXBuilder(extractedMsz);

      if (this.manifest.description) {
        builder.setDescription(this.manifest.description);
      }
      for (const [key, value] of Object.entries(this.manifest.extra)) {
        builder.setExtra(key, value);
      }
      this.addFilteredAnnotations(builder, this.targetScans(options), path.basename(tmpMszPath));
      const outputPath = builder.saveSync(output);

      return MSZXFile.open(outputPath);
    } finally {
      if (extractedMsz) extractedMsz.close();
      try {
        fs.rmSync(tmpDir, { recursive: true, force: true });
      } catch {
        /* ignore */
      }
    }
  }

  /**
   * Scan numbers selected by extract options, matching the Python binding's
   * `MSZXFile.extract`: the union of `scanNumbers` and the scans of
   * `indices`, restricted to `msLevel` when given; every scan when no
   * filter is set.
   */
  private targetScans(options?: ExtractOptions): Set<number> {
    const scans = this.positions.scans;
    const levels = this.positions.msLevels;
    const indices = options?.indices?.length ? options.indices : null;
    const scanNumbers = options?.scanNumbers?.length ? options.scanNumbers : null;
    const msLevel = options?.msLevel;
    const target = new Set<number>(scanNumbers ?? []);

    if (indices) {
      for (const idx of indices) {
        if (idx >= 0 && idx < scans.length) target.add(scans[idx]);
      }
    }
    if (msLevel !== undefined && msLevel !== null) {
      const atLevel = new Set<number>();
      for (let i = 0; i < scans.length; i++) {
        if (levels[i] === msLevel) atLevel.add(scans[i]);
      }
      if (indices || scanNumbers) {
        for (const scan of [...target]) {
          if (!atLevel.has(scan)) target.delete(scan);
        }
      } else {
        for (const scan of atLevel) target.add(scan);
      }
    }
    if (!indices && !scanNumbers && (msLevel === undefined || msLevel === null)) {
      for (const scan of scans) target.add(scan);
    }
    return target;
  }

  /**
   * Add every annotation of this archive to `builder`, filtered to `scans`
   * and stored as compressed mzTab. Members that fail are skipped with a
   * warning, as in the Python binding.
   */
  private addFilteredAnnotations(
    builder: MSZXBuilder,
    scans: Set<number>,
    spectraFile: string
  ): void {
    for (const entry of this.manifest.annotations) {
      if (!this.annotations.has(entry.filename)) continue;
      try {
        const reader = this.getPSMReader(entry.filename);
        let text: string;
        let isMzTab: boolean;
        if (reader instanceof MzTabReader) {
          text = reader.filter(scans);
          isMzTab = true;
        } else {
          text = writeMzTab(
            reader.psms.filter((psm) => scans.has(psm.scan_number)),
            {
              searchInfo: reader.searchInfo,
              msRunLocation: spectraFile,
              sourceFormat: reader.format,
              sourceName: entry.filename.replace(/\.zst$/i, ""),
            }
          );
          isMzTab = false;
        }
        builder.addMzTab(text, {
          filename: mzTabMemberName(entry.filename, true, isMzTab),
          description: entry.description,
          compressed: true,
        });
      } catch (err) {
        console.warn(
          `Warning: failed to filter annotation '${entry.filename}': ${(err as Error).message}`
        );
      }
    }
  }

  /**
   * File metadata, augmented with archive info.
   */
  describe(): Record<string, unknown> {
    const desc = super.describe();
    desc.archive = {
      path: this.archivePath,
      annotations: this.manifest.annotations,
    };
    return desc;
  }
}
