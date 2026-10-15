/**
 * PSM (Peptide-Spectrum Match) data structures and the shared query API.
 *
 * Field names deliberately use snake_case to mirror the Python `PSM`
 * dataclass, so PSMs serialized by either binding compare field-for-field.
 */

/** Value types an entry of {@link PSM.extra} may hold. */
export type ExtraValue = number | string | boolean;

/**
 * A single peptide-spectrum match.
 *
 * Optional numeric fields are `null` when absent (Python `None`).
 */
export interface PSM {
  /** Scan number from the mass spectrometer. */
  scan_number: number;
  /** Identified peptide sequence (without modifications). */
  peptide: string;
  /** Precursor charge state. */
  charge: number;
  /** Primary search/scoring metric. */
  score: number;
  /** Protein accessions, in source order. */
  proteins: string[];
  /** Zero-based index of the spectrum, if known. */
  spectrum_index: number | null;
  /** FDR-controlled q-value. */
  q_value: number | null;
  /** Posterior error probability. */
  pep: number | null;
  /** Retention time in seconds. */
  retention_time: number | null;
  /** Precursor m/z. */
  precursor_mz: number | null;
  /** Precursor neutral mass. */
  precursor_mass: number | null;
  /** Observed minus calculated mass. */
  mass_diff: number | null;
  /** Peptide sequence with modification annotations. */
  modified_peptide: string | null;
  /** Number of matched fragment ions. */
  num_matched_ions: number | null;
  /** Total number of theoretical fragment ions. */
  num_total_ions: number | null;
  /** Hit rank (1 = best). */
  rank: number;
  /** Whether this is a decoy hit. */
  is_decoy: boolean;
  /** Additional format-specific fields. */
  extra: Record<string, ExtraValue>;
}

/** A modification searched for, as declared by the search engine. */
export interface ModificationSpec {
  /** Mass shift in Daltons. */
  massdiff: number;
  /** Residue letter, or "N-term" / "C-term". */
  site: string;
  /**
   * One of "Anywhere", "Any N-term", "Any C-term", "Protein N-term",
   * "Protein C-term".
   */
  position: string;
}

/** Search-level metadata carried alongside a set of PSMs. */
export interface SearchInfo {
  /** Name of the search engine / rescoring tool. */
  software: string | null;
  /** Version string of that software. */
  software_version: string | null;
  /** Name of the score stored in {@link PSM.score}. */
  primary_score_name: string | null;
  /** Fixed modifications searched. */
  fixed_mods: ModificationSpec[];
  /** Variable modifications searched. */
  variable_mods: ModificationSpec[];
}

/**
 * Annotation formats understood by the PSM readers.
 *
 * `"tsv"` is a legacy manifest tag and is read as Percolator TSV.
 */
export type AnnotationFormat = "mztab" | "pepxml" | "percolator_tsv" | "tsv";

/**
 * Build a PSM, filling every unspecified field with its default.
 *
 * @param fields - Required core fields plus any optional overrides.
 * @returns A fully populated PSM.
 */
export function createPSM(
  fields: Pick<PSM, "scan_number" | "peptide" | "charge" | "score"> & Partial<PSM>
): PSM {
  return {
    scan_number: fields.scan_number,
    peptide: fields.peptide,
    charge: fields.charge,
    score: fields.score,
    proteins: fields.proteins ?? [],
    spectrum_index: fields.spectrum_index ?? null,
    q_value: fields.q_value ?? null,
    pep: fields.pep ?? null,
    retention_time: fields.retention_time ?? null,
    precursor_mz: fields.precursor_mz ?? null,
    precursor_mass: fields.precursor_mass ?? null,
    mass_diff: fields.mass_diff ?? null,
    modified_peptide: fields.modified_peptide ?? null,
    num_matched_ions: fields.num_matched_ions ?? null,
    num_total_ions: fields.num_total_ions ?? null,
    rank: fields.rank ?? 1,
    is_decoy: fields.is_decoy ?? false,
    extra: fields.extra ?? {},
  };
}

/**
 * Build a SearchInfo, filling every unspecified field with its default.
 *
 * @param fields - Optional overrides.
 * @returns A fully populated SearchInfo.
 */
export function createSearchInfo(fields?: Partial<SearchInfo>): SearchInfo {
  return {
    software: fields?.software ?? null,
    software_version: fields?.software_version ?? null,
    primary_score_name: fields?.primary_score_name ?? null,
    fixed_mods: fields?.fixed_mods ?? [],
    variable_mods: fields?.variable_mods ?? [],
  };
}

/**
 * An in-memory collection of PSMs with lookup by scan number.
 *
 * Every PSM reader (mzTab, pepXML, Percolator) returns one of these, so
 * annotations are queryable the same way regardless of their source format.
 */
export class PSMCollection implements Iterable<PSM> {
  /** All PSMs, in source order. */
  readonly psms: PSM[];
  /** Search-level metadata, if the source records it. */
  readonly searchInfo: SearchInfo;
  /** Format the PSMs were parsed from. */
  readonly format: AnnotationFormat;
  /** Original engine format the PSMs were converted from, if recorded. */
  readonly sourceFormat: string | null;
  /** Original engine file name the PSMs were converted from, if recorded. */
  readonly sourceName: string | null;

  private readonly scanIndex = new Map<number, number[]>();

  /**
   * @param psms - PSMs, in source order.
   * @param searchInfo - Search-level metadata.
   * @param format - Format the PSMs were parsed from.
   * @param sourceFormat - Original engine format, if known.
   * @param sourceName - Original engine file name, if known.
   */
  constructor(
    psms: PSM[],
    searchInfo: SearchInfo,
    format: AnnotationFormat,
    sourceFormat: string | null = null,
    sourceName: string | null = null
  ) {
    this.psms = psms;
    this.searchInfo = searchInfo;
    this.format = format;
    this.sourceFormat = sourceFormat;
    this.sourceName = sourceName;
    psms.forEach((psm, i) => {
      const bucket = this.scanIndex.get(psm.scan_number);
      if (bucket) {
        bucket.push(i);
      } else {
        this.scanIndex.set(psm.scan_number, [i]);
      }
    });
  }

  /** Number of PSMs. */
  get length(): number {
    return this.psms.length;
  }

  /** Iterate over all PSMs in source order. */
  [Symbol.iterator](): Iterator<PSM> {
    return this.psms[Symbol.iterator]();
  }

  /**
   * Get the PSM at a position.
   *
   * @param index - Zero-based position; negative values count from the end.
   * @returns The PSM, or undefined if out of range.
   */
  at(index: number): PSM | undefined {
    return this.psms.at(index);
  }

  /**
   * Get all PSMs for a scan number.
   *
   * @param scanNumber - Scan number to look up.
   * @returns Matching PSMs (empty if none).
   */
  getByScan(scanNumber: number): PSM[] {
    return (this.scanIndex.get(scanNumber) ?? []).map((i) => this.psms[i]);
  }

  /**
   * Get the highest-scoring PSM for a scan number. Ties resolve to the
   * earliest PSM, matching Python's `max()`.
   *
   * @param scanNumber - Scan number to look up.
   * @returns Best PSM by score, or null if the scan has none.
   */
  getBestByScan(scanNumber: number): PSM | null {
    let best: PSM | null = null;
    for (const psm of this.getByScan(scanNumber)) {
      if (best === null || psm.score > best.score) best = psm;
    }
    return best;
  }

  /**
   * Whether a scan number has any PSMs.
   *
   * @param scanNumber - Scan number to look up.
   */
  hasScan(scanNumber: number): boolean {
    return this.scanIndex.has(scanNumber);
  }

  /** Unique scan numbers with PSMs, in first-seen order. */
  get scanNumbers(): number[] {
    return [...this.scanIndex.keys()];
  }
}

const PY_FLOAT_RE =
  /^[+-]?(?:(?:\d(?:_?\d)*)?\.?\d(?:_?\d)*(?:[eE][+-]?\d(?:_?\d)*)?|\d(?:_?\d)*\.(?:[eE][+-]?\d(?:_?\d)*)?)$/;
const PY_SPECIAL_FLOAT_RE = /^([+-]?)(inf|infinity|nan)$/i;
const PY_INT_RE = /^[+-]?\d(?:_?\d)*$/;

/**
 * Parse a string the way Python's `float()` does: surrounding whitespace is
 * ignored, `_` digit separators and `inf`/`infinity`/`nan` (any case) are
 * accepted, and anything else — including the empty string — is rejected.
 *
 * @param value - Text to parse.
 * @returns The parsed number, or null if Python would raise ValueError.
 * @internal
 */
export function parsePyFloat(value: string | null | undefined): number | null {
  if (value === null || value === undefined) return null;
  const text = value.trim();
  const special = PY_SPECIAL_FLOAT_RE.exec(text);
  if (special) {
    if (special[2].toLowerCase() === "nan") return NaN;
    return special[1] === "-" ? -Infinity : Infinity;
  }
  if (!PY_FLOAT_RE.test(text)) return null;
  return Number(text.replace(/_/g, ""));
}

/**
 * Parse a string the way Python's `int()` does (base 10).
 *
 * @param value - Text to parse.
 * @returns The parsed integer, or null if Python would raise ValueError.
 * @internal
 */
export function parsePyInt(value: string | null | undefined): number | null {
  if (value === null || value === undefined) return null;
  const text = value.trim();
  if (!PY_INT_RE.test(text)) return null;
  return Number(text.replace(/_/g, ""));
}
