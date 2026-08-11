/**
 * MSZX file format manifest
 *
 * MSZX is a bundled archive format that combines:
 * - Compressed mass spectrometry data (MSZ)
 * - Optional annotation files
 * - Internal manifest for self-description
 */

/**
 * Entry describing an annotation file in the MSZX archive.
 */
export interface AnnotationEntry {
  /** Filename of the annotation file within the archive */
  filename: string;
  /** Format of the annotation file */
  format: "percolator_tsv" | "pepxml" | "tsv";
  /** Whether the file is compressed with zstd */
  compressed: boolean;
  /** Optional human-readable description */
  description?: string;
  /** Number of records/PSMs in the file */
  num_records?: number;
}

/**
 * MSZX manifest data structure for JSON serialization.
 */
export interface MSZXManifestData {
  /** Manifest format version */
  version: string;
  /** Creation timestamp in ISO 8601 format */
  created_at: string;
  /** Name of the MSZ file within the archive */
  spectra_file: string;
  /** Total number of spectra in the MSZ file */
  num_spectra: number;
  /** List of annotation file entries */
  annotations: AnnotationEntry[];
  /** Key used to join spectra with annotations (e.g., 'scan_number') */
  join_key: string;
  /** Optional human-readable description of the archive */
  description?: string;
  /** Optional original source file name */
  source_file?: string;
  /** Optional custom metadata */
  extra?: Record<string, unknown>;
}

/**
 * Manifest describing the contents of an MSZX archive.
 *
 * This is stored as manifest.json inside the archive for self-description.
 */
export class MSZXManifest {
  version: string;
  created_at: string;
  spectra_file: string;
  num_spectra: number;
  annotations: AnnotationEntry[];
  join_key: string;
  description?: string;
  source_file?: string;
  extra: Record<string, unknown>;

  /**
   * Create a new MSZX manifest.
   *
   * @param data - Optional partial manifest data. Unspecified fields will use defaults.
   */
  constructor(data?: Partial<MSZXManifestData>) {
    this.version = data?.version || "1.0";
    this.created_at = data?.created_at || new Date().toISOString();
    this.spectra_file = data?.spectra_file || "spectra.msz";
    this.num_spectra = data?.num_spectra || 0;
    this.annotations = data?.annotations || [];
    this.join_key = data?.join_key || "scan_number";
    this.description = data?.description;
    this.source_file = data?.source_file;
    this.extra = data?.extra || {};
  }

  /**
   * Convert to plain object for JSON serialization.
   *
   * @returns Plain object representation of the manifest
   */
  toJSON(): MSZXManifestData {
    const data: MSZXManifestData = {
      version: this.version,
      created_at: this.created_at,
      spectra_file: this.spectra_file,
      num_spectra: this.num_spectra,
      annotations: this.annotations,
      join_key: this.join_key,
    };

    if (this.description !== undefined) {
      data.description = this.description;
    }
    if (this.source_file !== undefined) {
      data.source_file = this.source_file;
    }
    if (Object.keys(this.extra).length > 0) {
      data.extra = this.extra;
    }

    return data;
  }

  /**
   * Serialize to JSON string.
   *
   * @param indent - Number of spaces for indentation (default: 2)
   * @returns JSON string representation
   */
  toString(indent: number = 2): string {
    return JSON.stringify(this.toJSON(), null, indent);
  }

  /** Highest MSZX manifest major version this binding can read. */
  static readonly MAX_SUPPORTED_MAJOR = 2;

  /**
   * Create from plain object.
   *
   * @param data - Manifest data object
   * @returns New MSZXManifest instance
   * @throws Error if the manifest is a newer major version than this build
   *   supports, or is a multi-file/batch archive — those carry N spectra files
   *   and must go through {@link MSZXBatchManifest} instead. Refusing loudly
   *   avoids silently mis-reading a batch archive as a single-file one.
   */
  static fromJSON(data: MSZXManifestData): MSZXManifest {
    const raw = data as unknown as Record<string, unknown>;
    rejectUnsupportedMajor(raw, MSZXManifest.MAX_SUPPORTED_MAJOR);
    if (isBatchManifest(raw)) {
      throw new Error(
        "this .mszx is a multi-file (batch) archive; parse it with " +
          "MSZXBatchManifest.fromJSON() and open it with MSZXBatchFile"
      );
    }
    return new MSZXManifest(data);
  }

  /**
   * Parse from JSON string.
   *
   * @param json - JSON string to parse
   * @returns New MSZXManifest instance
   */
  static parse(json: string): MSZXManifest {
    const parsed: unknown = JSON.parse(json);
    if (typeof parsed !== "object" || parsed === null) {
      throw new TypeError("Invalid manifest JSON: expected object");
    }
    return MSZXManifest.fromJSON(parsed as MSZXManifestData);
  }
}

/** Leading integer of the manifest `version`; 1 when unparseable. */
function manifestMajor(raw: Record<string, unknown>): number {
  const version = typeof raw.version === "string" ? raw.version : "1.0";
  const parsed = parseInt(version.split(".")[0], 10);
  return Number.isNaN(parsed) ? 1 : parsed;
}

function rejectUnsupportedMajor(raw: Record<string, unknown>, maxMajor: number): void {
  if (manifestMajor(raw) > maxMajor) {
    throw new Error(
      `MSZX manifest version '${String(raw.version)}' is newer than this build ` +
        `supports (max major ${maxMajor}); please upgrade mscompress`
    );
  }
}

/**
 * True if `raw` describes a v2 multi-file ("batch") archive.
 *
 * Keyed on the payload shape rather than the version string alone, so a
 * mislabeled `"1.0"` manifest carrying `spectra_files` is still routed to the
 * batch reader instead of being mis-read as single-file.
 */
export function isBatchManifest(raw: Record<string, unknown>): boolean {
  return "spectra_files" in raw || raw.container === "batch";
}

/** One member of a v2 ("batch") MSZX archive. */
export interface SpectraFileEntryData {
  /** Tar member name of the MSZ payload (e.g. `"sample.msz"`) */
  entry: string;
  /** Basename of the source mzML — the source directory is not recorded */
  original?: string;
  /** Payload size in bytes, matching the tar header */
  size?: number;
  /** Spectrum count; absent in archives written before this field existed */
  num_spectra?: number;
  /** Column used to join annotations to spectra */
  join_key?: string;
  /** Annotation members attached to this entry */
  annotations?: AnnotationEntry[];
}

/** Manifest data structure of a v2 multi-file ("batch") MSZX archive. */
export interface MSZXBatchManifestData {
  version: string;
  container: string;
  spectra_files: SpectraFileEntryData[];
  description?: string;
  extra?: Record<string, unknown>;
}

/**
 * Manifest of a v2 multi-file ("batch") MSZX archive.
 *
 * A batch archive bundles N independent MSZ files written by the shared C
 * batch writer. Unlike {@link MSZXManifest} it carries no `created_at` — the
 * format is deliberately timestamp-free so identical inputs produce
 * byte-identical archives.
 *
 * Every field beyond `entry`/`original`/`size` is optional, so archives written
 * before those fields existed still parse.
 */
export class MSZXBatchManifest {
  version: string;
  container: string;
  spectra_files: SpectraFileEntryData[];
  description?: string;
  extra?: Record<string, unknown>;

  /** Highest batch manifest major version this binding can read. */
  static readonly MAX_SUPPORTED_MAJOR = 2;

  constructor(data?: Partial<MSZXBatchManifestData>) {
    this.version = data?.version ?? "2.0";
    this.container = data?.container ?? "batch";
    this.spectra_files = (data?.spectra_files ?? []).map((e) => ({
      entry: e.entry,
      original: e.original ?? "",
      size: e.size ?? 0,
      num_spectra: e.num_spectra,
      join_key: e.join_key ?? "scan_number",
      annotations: e.annotations ?? [],
    }));
    this.description = data?.description;
    this.extra = data?.extra;
  }

  /** Number of spectra files in the archive. */
  get length(): number {
    return this.spectra_files.length;
  }

  toJSON(): MSZXBatchManifestData {
    const data: MSZXBatchManifestData = {
      version: this.version,
      container: this.container,
      spectra_files: this.spectra_files,
    };
    if (this.description) data.description = this.description;
    if (this.extra) data.extra = this.extra;
    return data;
  }

  toString(indent = 2): string {
    return JSON.stringify(this.toJSON(), null, indent);
  }

  /**
   * @throws Error if the manifest is a newer major version than this build
   *   supports, or is not a batch manifest at all.
   */
  static fromJSON(data: MSZXBatchManifestData): MSZXBatchManifest {
    const raw = data as unknown as Record<string, unknown>;
    rejectUnsupportedMajor(raw, MSZXBatchManifest.MAX_SUPPORTED_MAJOR);
    if (!isBatchManifest(raw)) {
      throw new Error(
        "not a multi-file (batch) MSZX manifest; use MSZXManifest for " +
          "single-file archives"
      );
    }
    return new MSZXBatchManifest(data);
  }

  static parse(json: string): MSZXBatchManifest {
    const parsed: unknown = JSON.parse(json);
    if (typeof parsed !== "object" || parsed === null) {
      throw new TypeError("Invalid manifest JSON: expected object");
    }
    return MSZXBatchManifest.fromJSON(parsed as MSZXBatchManifestData);
  }

  /**
   * Adapt a v1 single-file manifest into a one-member batch manifest.
   *
   * Lets one container type read every `.mszx`: a v1 archive is simply a
   * collection of one. `container` is reported as `"single"` and `version` is
   * preserved, so callers can still tell the two apart.
   *
   * `size` is left at 0 — the v1 manifest does not record the payload length.
   * {@link MSZXBatchFile.open} fills it in from the tar header.
   */
  static fromSingleFile(manifest: MSZXManifest): MSZXBatchManifest {
    return new MSZXBatchManifest({
      version: manifest.version,
      container: "single",
      spectra_files: [
        {
          entry: manifest.spectra_file,
          original: manifest.source_file ?? "",
          size: 0,
          num_spectra: manifest.num_spectra,
          join_key: manifest.join_key,
          annotations: manifest.annotations,
        },
      ],
      description: manifest.description,
      extra: manifest.extra,
    });
  }
}
