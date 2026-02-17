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
  format: 'percolator_tsv' | 'pepxml' | 'tsv';
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
  extra?: Record<string, any>;
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
  extra: Record<string, any>;

  /**
   * Create a new MSZX manifest.
   *
   * @param data - Optional partial manifest data. Unspecified fields will use defaults.
   */
  constructor(data?: Partial<MSZXManifestData>) {
    this.version = data?.version || '1.0';
    this.created_at = data?.created_at || new Date().toISOString();
    this.spectra_file = data?.spectra_file || 'spectra.msz';
    this.num_spectra = data?.num_spectra || 0;
    this.annotations = data?.annotations || [];
    this.join_key = data?.join_key || 'scan_number';
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

  /**
   * Create from plain object.
   *
   * @param data - Manifest data object
   * @returns New MSZXManifest instance
   */
  static fromJSON(data: MSZXManifestData): MSZXManifest {
    return new MSZXManifest(data);
  }

  /**
   * Parse from JSON string.
   *
   * @param json - JSON string to parse
   * @returns New MSZXManifest instance
   */
  static parse(json: string): MSZXManifest {
    return MSZXManifest.fromJSON(JSON.parse(json));
  }
}
