/**
 * Percolator TSV / PIN → PSM conversion.
 *
 * A faithful port of the Python `TSVReader` (`_parse` / `_parse_row`), so
 * both bindings produce identical PSMs from the same file, including its
 * heuristics for scan number, charge, peptide clean-up and primary score.
 */

import {
  createPSM,
  createSearchInfo,
  parsePyFloat,
  parsePyInt,
  type ExtraValue,
  type PSM,
  type SearchInfo,
} from "@/annotations/psm.js";

/** Options for {@link parsePercolator}. */
export interface PercolatorOptions {
  /** Substring identifying decoy proteins when there is no Label column (default `"DECOY_"`). */
  decoyPrefix?: string;
}

/** `file.scan.scan.charge` with an optional `_rank` suffix (anchored at start, like `re.match`). */
const SPECID_PATTERN = /^.*?\.(\d+)\.(\d+)\.(\d+)(?:_(\d+))?/;

/** Columns that never go into `extra`. */
const EXCLUDE_COLS = new Set([
  "SpecId",
  "PSMId",
  "Label",
  "ScanNr",
  "Peptide",
  "peptide",
  "Proteins",
  "proteinIds",
]);

/** Columns never used as the fallback primary score. */
const NON_SCORE_COLS = new Set([
  "ExpMass",
  "CalcMass",
  "deltCn",
  "q-value",
  "posterior_error_prob",
  "ScanNr",
  "Charge",
  "charge",
]);

/** Python `str.splitlines()` boundaries. */
// eslint-disable-next-line no-control-regex -- FS/GS/RS separators are line breaks to Python
const SPLITLINES_RE = /\r\n|[\n\r\v\f\x1c\x1d\x1e\x85\u2028\u2029]/;

/**
 * Split one line like Python's `csv.reader` with default dialect settings
 * (quotechar `"`, doublequote, no skipinitialspace).
 */
function splitCsvLine(line: string, delimiter: string): string[] {
  const fields: string[] = [];
  let field = "";
  let inQuotes = false;
  let atStart = true;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (inQuotes) {
      if (c === '"') {
        if (line[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          inQuotes = false;
        }
      } else {
        field += c;
      }
    } else if (c === delimiter) {
      fields.push(field);
      field = "";
      atStart = true;
      continue;
    } else if (c === '"' && atStart) {
      inQuotes = true;
    } else {
      field += c;
    }
    atStart = false;
  }
  fields.push(field);
  return fields;
}

/**
 * Approximate `csv.Sniffer().sniff()` delimiter detection: the candidate that
 * occurs the same, non-zero number of times on the most sample lines wins.
 * Falls back to tab.
 */
function sniffDelimiter(sample: string): string {
  const lines = sample.split(SPLITLINES_RE).filter((l) => l.length > 0);
  if (lines.length > 1 && !sample.endsWith("\n")) lines.pop(); // last line may be truncated
  let best = "\t";
  let bestScore = 0;
  for (const delim of ["\t", ",", ";", " ", ":"]) {
    const counts = lines.map((l) => l.split(delim).length - 1);
    const headerCount = counts[0] ?? 0;
    if (headerCount === 0) continue;
    const score = counts.filter((c) => c === headerCount).length;
    if (score > bestScore) {
      best = delim;
      bestScore = score;
    }
  }
  return best;
}

/** One `csv.DictReader` row: missing cells are `null`, surplus cells land under `rest`. */
interface CsvRow {
  values: Map<string, string | null>;
  rest: string[] | null;
}

function toRow(headers: string[], cells: string[]): CsvRow {
  const values = new Map<string, string | null>();
  headers.forEach((h, i) => values.set(h, i < cells.length ? cells[i] : null));
  return { values, rest: cells.length > headers.length ? cells.slice(headers.length) : null };
}

/** Python `row.get(a, row.get(b, ""))`. */
function getEither(row: CsvRow, a: string, b: string): string | null {
  if (row.values.has(a)) return row.values.get(a) ?? null;
  if (row.values.has(b)) return row.values.get(b) ?? null;
  return "";
}

function parseRow(row: CsvRow, headers: string[], decoyPrefix: string): PSM | null {
  const v = row.values;
  const specId = getEither(row, "SpecId", "PSMId") ?? "";

  let scanNumber = 0;
  let charge = 0;
  const match = SPECID_PATTERN.exec(specId);
  if (match) {
    scanNumber = Number(match[1]);
    charge = Number(match[3]);
  } else {
    if (v.has("ScanNr")) {
      const scan = parsePyInt(v.get("ScanNr"));
      if (scan === null) return null;
      scanNumber = scan;
    }
    const chargeText = v.has("Charge") ? v.get("Charge") : v.has("charge") ? v.get("charge") : "2";
    const parsed = parsePyInt(chargeText);
    if (parsed === null) return null;
    charge = parsed;
  }

  let peptide = getEither(row, "Peptide", "peptide") ?? "";
  // Strip single-letter flanking residues (K.PEPTIDE.R → PEPTIDE), keeping
  // any "." inside the peptide, e.g. a mass shift like M[15.9949].
  const flanked = /^[^.]?\.(.+)\.[^.]?$/.exec(peptide);
  if (flanked) peptide = flanked[1];

  // A short row leaves Label as None, which Python treats as "no Label column".
  const labelText = v.get("Label") ?? undefined;
  let isDecoy = false;
  if (labelText !== undefined) {
    const label = parsePyInt(labelText);
    if (label === null) return null;
    isDecoy = label === -1;
  }

  const proteinsText = getEither(row, "Proteins", "proteinIds") ?? "";
  const sep = proteinsText.includes(";") ? ";" : ",";
  const proteins = proteinsText
    .split(sep)
    .map((p) => p.trim())
    .filter((p) => p.length > 0);

  if (labelText === undefined) {
    const values = [...v.values()].map((x) => (x === null ? "None" : x));
    if (row.rest) values.push(row.rest.join(" "));
    isDecoy = values.some((x) => x.includes(decoyPrefix));
    if (!isDecoy && proteins.length > 0) isDecoy = proteins.some((p) => p.includes(decoyPrefix));
  }

  let score = 0.0;
  const extra: Record<string, ExtraValue> = {};
  for (const h of headers) {
    if (EXCLUDE_COLS.has(h)) continue;
    const val = v.has(h) ? v.get(h) : "";
    if (val === null || val === undefined) return null; // Python: float(None) -> TypeError
    const num = parsePyFloat(val);
    if (num === null) {
      extra[h] = val;
      continue;
    }
    extra[h] = num;
    if (h.toLowerCase() === "score") {
      score = num;
    } else if (score === 0.0 && !NON_SCORE_COLS.has(h)) {
      score = num;
    }
  }

  return createPSM({
    scan_number: scanNumber,
    peptide,
    charge,
    score,
    proteins,
    q_value: v.has("q-value") ? parsePyFloat(v.get("q-value")) : null,
    pep: v.has("posterior_error_prob") ? parsePyFloat(v.get("posterior_error_prob")) : null,
    is_decoy: isDecoy,
    extra,
  });
}

/**
 * Convert a Percolator output TSV or PIN file into PSMs plus search metadata.
 *
 * @param data - File text or UTF-8 bytes.
 * @param options - Decoy prefix.
 * @returns PSMs (malformed rows skipped) and search metadata: software is
 *   `"Percolator"` for `PSMId`-headed output, and `primary_score_name` is
 *   `"score"` when a score column exists.
 */
export function parsePercolator(
  data: string | Buffer,
  options?: PercolatorOptions
): { psms: PSM[]; searchInfo: SearchInfo } {
  const text = typeof data === "string" ? data : data.toString("utf-8");
  const decoyPrefix = options?.decoyPrefix ?? "DECOY_";
  const lines = text.split(SPLITLINES_RE);
  // splitlines() yields no trailing empty element.
  if (lines.length > 0 && lines[lines.length - 1] === "") lines.pop();
  if (lines.length === 0) return { psms: [], searchInfo: createSearchInfo() };

  const headerLine = lines[0].trim();
  const delimiter =
    headerLine.startsWith("SpecId") || headerLine.startsWith("PSMId")
      ? "\t"
      : sniffDelimiter(text.slice(0, 4096));
  const headers = headerLine.split(delimiter);

  const psms: PSM[] = [];
  for (const line of lines.slice(1)) {
    if (line === "") continue; // csv.DictReader skips empty rows
    const psm = parseRow(toRow(headers, splitCsvLine(line, delimiter)), headers, decoyPrefix);
    if (psm) psms.push(psm);
  }

  const searchInfo = createSearchInfo({
    software: headers[0] === "PSMId" ? "Percolator" : null,
    primary_score_name: headers.some((h) => h.toLowerCase() === "score") ? "score" : null,
  });
  return { psms, searchInfo };
}
