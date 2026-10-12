/**
 * mzTab 1.0 (Summary mode, Identification type, PSM section) writer and
 * reader.
 *
 * mzTab is the only format MSZX archives store search results in; engine
 * output (pepXML, Percolator) is converted on the way in. The layout written
 * here is shared with the Python binding — see the mscompress mzTab spec —
 * so both produce and accept the same files.
 */

import {
  createPSM,
  createSearchInfo,
  parsePyFloat,
  PSMCollection,
  type ExtraValue,
  type ModificationSpec,
  type PSM,
  type SearchInfo,
} from "@/annotations/psm.js";

/** Options for {@link writeMzTab}. */
export interface MzTabWriteOptions {
  /** Search-level metadata for the MTD section. */
  searchInfo?: SearchInfo | null;
  /** Spectra file name (e.g. `"sample.msz"`), written as `ms_run[1]-location`. */
  msRunLocation?: string | null;
  /** Engine format the PSMs were converted from (e.g. `"pepxml"`). */
  sourceFormat?: string | null;
  /** Engine file name the PSMs were converted from (e.g. `"run.pepXML"`). */
  sourceName?: string | null;
  /** Written as `mzTab-ID` when given. */
  title?: string | null;
}

/** Fixed PSH columns, in the order the writer emits them. */
const PSH_COLUMNS = [
  "sequence",
  "PSM_ID",
  "accession",
  "unique",
  "database",
  "database_version",
  "search_engine",
  "search_engine_score[1]",
  "search_engine_score[2]",
  "search_engine_score[3]",
  "modifications",
  "retention_time",
  "charge",
  "exp_mass_to_charge",
  "calc_mass_to_charge",
  "spectra_ref",
  "pre",
  "post",
  "start",
  "end",
  "opt_global_cv_MS:1002217_decoy_peptide",
  "opt_global_modified_sequence",
  "opt_global_rank",
  "opt_global_scan_number",
  "opt_global_spectrum_index",
  "opt_global_precursor_mass",
  "opt_global_mass_diff",
  "opt_global_num_matched_ions",
  "opt_global_num_total_ions",
];

const EXTRA_PREFIX = "opt_global_extra_";
const DECOY_COLUMN = "opt_global_cv_MS:1002217_decoy_peptide";

/** `opt_global_*` columns with a dedicated PSM field (never routed to `extra`). */
const KNOWN_OPT_COLUMNS = new Set([
  DECOY_COLUMN,
  "opt_global_decoy",
  "opt_global_modified_sequence",
  "opt_global_rank",
  "opt_global_scan_number",
  "opt_global_spectrum_index",
  "opt_global_precursor_mass",
  "opt_global_mass_diff",
  "opt_global_num_matched_ions",
  "opt_global_num_total_ions",
]);

/** Replace tab/CR/LF inside a cell with a single space. */
function clean(value: string): string {
  return value.replace(/[\t\r\n]/g, " ");
}

/** Format a float: shortest round-trip form, `NaN`, `INF`, `-INF`. */
function formatFloat(value: number): string {
  if (Number.isNaN(value)) return "NaN";
  if (value === Infinity) return "INF";
  if (value === -Infinity) return "-INF";
  return String(value);
}

function formatOptFloat(value: number | null | undefined): string {
  return value === null || value === undefined ? "null" : formatFloat(value);
}

function formatOptInt(value: number | null | undefined): string {
  return value === null || value === undefined ? "null" : String(Math.trunc(value));
}

function formatOptString(value: string | null | undefined): string {
  return value === null || value === undefined ? "null" : clean(value);
}

function formatExtra(value: ExtraValue | null | undefined): string {
  if (value === null || value === undefined) return "null";
  if (typeof value === "number") return formatFloat(value);
  if (typeof value === "boolean") return value ? "true" : "false";
  return clean(String(value));
}

/** Format a `[cv, accession, name, value]` param. */
function param(cv: string, accession: string, name: string, value: string = ""): string {
  return `[${cv}, ${accession}, ${clean(name)}, ${clean(value)}]`;
}

function isPercolator(info: SearchInfo): boolean {
  return (info.software ?? "").toLowerCase() === "percolator";
}

function softwareParam(info: SearchInfo): string {
  if (isPercolator(info)) return param("MS", "MS:1001490", "Percolator");
  return param("MS", "MS:1001456", "analysis software", info.software ?? "");
}

function primaryScoreParam(info: SearchInfo): string {
  const name = info.primary_score_name ?? "";
  switch (name.toLowerCase()) {
    case "hyperscore":
      return param("MS", "MS:1001331", "X!Tandem:hyperscore");
    case "expect":
      return param("MS", "MS:1001330", "X!Tandem:expect");
    case "xcorr":
      return param("MS", "MS:1001155", "SEQUEST:xcorr");
    case "score":
      if (isPercolator(info)) return param("MS", "MS:1001492", "percolator:score");
      break;
    case "":
      return param("MS", "MS:1001153", "search engine specific score");
  }
  return param("", "", name);
}

function modParam(mod: ModificationSpec): string {
  const sign = mod.massdiff < 0 ? "-" : "+";
  return param("CHEMMOD", `CHEMMOD:${sign}${formatFloat(Math.abs(mod.massdiff))}`, "");
}

/**
 * Serialize PSMs to mzTab 1.0 text (Summary mode, Identification type).
 *
 * A PSM with N proteins becomes N consecutive rows sharing one `PSM_ID`;
 * a PSM without proteins becomes one row with `accession = null`.
 *
 * @param psms - PSMs to write, in output order.
 * @param options - MTD metadata.
 * @returns The mzTab document, `\n`-terminated.
 */
export function writeMzTab(psms: Iterable<PSM>, options?: MzTabWriteOptions): string {
  const info = options?.searchInfo ?? createSearchInfo();
  const sourceFormat = options?.sourceFormat || null;
  const sourceName = options?.sourceName || null;
  const lines: string[] = [];
  const mtd = (key: string, value: string): void => {
    lines.push(`MTD\t${key}\t${value}`);
  };

  mtd("mzTab-version", "1.0.0");
  mtd("mzTab-mode", "Summary");
  mtd("mzTab-type", "Identification");
  if (options?.title) mtd("mzTab-ID", clean(options.title));

  let description = "PSMs written by mscompress";
  if (sourceFormat) {
    description = `Converted by mscompress from ${sourceFormat}`;
    if (sourceName) description += ` (${sourceName})`;
  }
  mtd("description", clean(description));
  mtd(
    "ms_run[1]-location",
    options?.msRunLocation ? clean(`file://${options.msRunLocation}`) : "null"
  );

  const software = softwareParam(info);
  mtd("software[1]", software);
  if (info.software_version) {
    mtd("software[1]-setting[1]", clean(`version=${info.software_version}`));
  }

  const percolator = isPercolator(info);
  mtd("psm_search_engine_score[1]", primaryScoreParam(info));
  mtd(
    "psm_search_engine_score[2]",
    percolator
      ? param("MS", "MS:1001491", "percolator:Q value")
      : param("MS", "MS:1002354", "PSM-level q-value")
  );
  mtd(
    "psm_search_engine_score[3]",
    percolator
      ? param("MS", "MS:1001493", "percolator:PEP")
      : param("", "", "posterior error probability")
  );

  const writeMods = (kind: "fixed_mod" | "variable_mod", mods: ModificationSpec[]): void => {
    if (mods.length === 0) {
      mtd(
        `${kind}[1]`,
        kind === "fixed_mod"
          ? param("MS", "MS:1002453", "No fixed modifications searched")
          : param("MS", "MS:1002454", "No variable modifications searched")
      );
      return;
    }
    mods.forEach((mod, i) => {
      const k = i + 1;
      mtd(`${kind}[${k}]`, modParam(mod));
      mtd(`${kind}[${k}]-site`, clean(mod.site));
      mtd(`${kind}[${k}]-position`, clean(mod.position));
    });
  };
  writeMods("fixed_mod", info.fixed_mods);
  writeMods("variable_mod", info.variable_mods);

  let custom = 0;
  if (sourceFormat) mtd(`custom[${++custom}]`, param("", "", "source_format", sourceFormat));
  if (sourceName) mtd(`custom[${++custom}]`, param("", "", "source_file", sourceName));

  const psmList = [...psms];
  const extraKeys: string[] = [];
  const seenExtra = new Set<string>();
  for (const psm of psmList) {
    for (const key of Object.keys(psm.extra ?? {})) {
      if (!seenExtra.has(key)) {
        seenExtra.add(key);
        extraKeys.push(key);
      }
    }
  }
  const extraHeaders = extraKeys.map((k) => EXTRA_PREFIX + k.replace(/\s/g, "_"));
  lines.push(["PSH", ...PSH_COLUMNS, ...extraHeaders].join("\t"));

  psmList.forEach((psm, i) => {
    const proteins = psm.proteins ?? [];
    const accessions: (string | null)[] = proteins.length > 0 ? proteins : [null];
    const unique = proteins.length === 0 ? "null" : proteins.length === 1 ? "1" : "0";
    const spectraRef =
      psm.spectrum_index !== null && psm.spectrum_index !== undefined
        ? `ms_run[1]:index=${Math.trunc(psm.spectrum_index)}`
        : `ms_run[1]:scan=${Math.trunc(psm.scan_number)}`;
    const tail = [
      psm.is_decoy ? "1" : "0",
      formatOptString(psm.modified_peptide),
      formatOptInt(psm.rank),
      formatOptInt(psm.scan_number),
      formatOptInt(psm.spectrum_index),
      formatOptFloat(psm.precursor_mass),
      formatOptFloat(psm.mass_diff),
      formatOptInt(psm.num_matched_ions),
      formatOptInt(psm.num_total_ions),
      ...extraKeys.map((k) => formatExtra(psm.extra?.[k])),
    ];
    for (const accession of accessions) {
      lines.push(
        [
          "PSM",
          clean(psm.peptide),
          String(i + 1),
          formatOptString(accession),
          unique,
          "null",
          "null",
          software,
          formatFloat(psm.score),
          formatOptFloat(psm.q_value),
          formatOptFloat(psm.pep),
          "null",
          formatOptFloat(psm.retention_time),
          formatOptInt(psm.charge),
          formatOptFloat(psm.precursor_mz),
          "null",
          spectraRef,
          "null",
          "null",
          "null",
          "null",
          ...tail,
        ].join("\t")
      );
    }
  });

  return lines.join("\n") + "\n";
}

/** A parsed mzTab `[cv, accession, name, value]` parameter. */
export interface MzTabParam {
  cv: string;
  accession: string;
  name: string;
  value: string;
}

function unquote(text: string): string {
  const t = text.trim();
  return t.length >= 2 && t.startsWith('"') && t.endsWith('"') ? t.slice(1, -1) : t;
}

/**
 * Parse an mzTab param string `[cv, accession, name, value]`.
 *
 * Commas inside the name are tolerated (everything between the second and
 * the last comma is the name).
 *
 * @param text - Param text.
 * @returns The parsed param, or null if `text` is not bracketed.
 */
export function parseMzTabParam(text: string): MzTabParam | null {
  const t = text.trim();
  if (!t.startsWith("[") || !t.endsWith("]")) return null;
  const parts = t.slice(1, -1).split(",");
  while (parts.length < 4) parts.push("");
  return {
    cv: unquote(parts[0]),
    accession: unquote(parts[1]),
    name: unquote(parts.slice(2, -1).join(",")),
    value: unquote(parts[parts.length - 1]),
  };
}

/** True for a missing cell: absent, empty, or the literal `null`. */
function isNull(value: string | undefined): value is undefined {
  return value === undefined || value === "" || value === "null";
}

function optFloat(value: string | undefined): number | null {
  return isNull(value) ? null : parsePyFloat(value);
}

function optInt(value: string | undefined): number | null {
  const parsed = optFloat(value);
  return parsed === null || !Number.isFinite(parsed) ? null : Math.trunc(parsed);
}

function parseExtraValue(value: string): ExtraValue {
  const num = parsePyFloat(value);
  if (num !== null) return num;
  if (value === "true") return true;
  if (value === "false") return false;
  return value;
}

/** Column indexes resolved from a PSH header line. */
interface PshLayout {
  columns: Map<string, number>;
  /** (column index, extra key) for every column routed to `extra`. */
  extras: Array<[number, string]>;
}

function buildLayout(header: string[]): PshLayout {
  const columns = new Map<string, number>();
  const extras: Array<[number, string]> = [];
  header.forEach((name, i) => {
    if (!columns.has(name)) columns.set(name, i);
    if (name.startsWith(EXTRA_PREFIX)) {
      extras.push([i, name.slice(EXTRA_PREFIX.length)]);
    } else if (name.startsWith("opt_") && !KNOWN_OPT_COLUMNS.has(name)) {
      extras.push([i, name.startsWith("opt_global_") ? name.slice("opt_global_".length) : name]);
    }
  });
  return { columns, extras };
}

const SCAN_REF_RE = /scan=(\d+)/;
const INDEX_REF_RE = /index=(\d+)/;

/** Scan number of one PSM row, using spectra_ref and the scan_number override. */
function rowScanNumber(cell: (name: string) => string | undefined): number {
  const override = optInt(cell("opt_global_scan_number"));
  if (override !== null) return override;
  const ref = cell("spectra_ref");
  const match = ref ? SCAN_REF_RE.exec(ref) : null;
  return match ? Number(match[1]) : 0;
}

/** Split text into lines, dropping a trailing CR from each. */
function splitLines(text: string): string[] {
  return text.split("\n").map((l) => (l.endsWith("\r") ? l.slice(0, -1) : l));
}

/** Score column roles resolved from MTD `psm_search_engine_score[n]` params. */
interface ScoreRoles {
  primary: number | null;
  qValue: number | null;
  pep: number | null;
  primaryParam: MzTabParam | null;
}

const Q_VALUE_ACCESSIONS = new Set(["MS:1001491", "MS:1002354", "MS:1001364"]);

function resolveScoreRoles(mtd: Map<string, string>): ScoreRoles {
  const scores: Array<[number, MzTabParam]> = [];
  for (const [key, value] of mtd) {
    const m = /^psm_search_engine_score\[(\d+)\]$/.exec(key);
    if (!m) continue;
    const p = parseMzTabParam(value);
    if (p) scores.push([Number(m[1]), p]);
  }
  if (scores.length === 0) {
    return { primary: 1, qValue: null, pep: null, primaryParam: null };
  }
  scores.sort((a, b) => a[0] - b[0]);
  const roles: ScoreRoles = { primary: null, qValue: null, pep: null, primaryParam: null };
  for (const [n, p] of scores) {
    const name = p.name.toLowerCase();
    if (
      Q_VALUE_ACCESSIONS.has(p.accession) ||
      name.includes("q-value") ||
      name.includes("q value")
    ) {
      if (roles.qValue === null) roles.qValue = n;
    } else if (p.accession === "MS:1001493" || name.includes("posterior error") || name === "pep") {
      if (roles.pep === null) roles.pep = n;
    } else if (roles.primary === null) {
      roles.primary = n;
      roles.primaryParam = p;
    }
  }
  return roles;
}

function parseMods(
  mtd: Map<string, string>,
  kind: "fixed_mod" | "variable_mod"
): ModificationSpec[] {
  const entries: Array<[number, ModificationSpec]> = [];
  const re = new RegExp(`^${kind}\\[(\\d+)\\]$`);
  for (const [key, value] of mtd) {
    const m = re.exec(key);
    if (!m) continue;
    const p = parseMzTabParam(value);
    if (!p || !p.accession.toUpperCase().startsWith("CHEMMOD:")) continue;
    const massdiff = parsePyFloat(p.accession.slice("CHEMMOD:".length));
    if (massdiff === null) continue;
    const k = m[1];
    entries.push([
      Number(k),
      {
        massdiff,
        site: mtd.get(`${kind}[${k}]-site`) ?? "",
        position: mtd.get(`${kind}[${k}]-position`) || "Anywhere",
      },
    ]);
  }
  return entries.sort((a, b) => a[0] - b[0]).map(([, mod]) => mod);
}

/** Reverse of the writer's score-name → CV param mapping. */
const SCORE_ACCESSION_NAMES: Record<string, string | null> = {
  "MS:1001331": "hyperscore",
  "MS:1001330": "expect",
  "MS:1001155": "xcorr",
  "MS:1001492": "score",
  "MS:1001153": null,
};

function primaryScoreName(p: MzTabParam | null): string | null {
  if (!p) return null;
  if (p.accession in SCORE_ACCESSION_NAMES) return SCORE_ACCESSION_NAMES[p.accession];
  return p.name || null;
}

function parseSearchInfo(mtd: Map<string, string>, roles: ScoreRoles): SearchInfo {
  let software: string | null = null;
  const sw = parseMzTabParam(mtd.get("software[1]") ?? "");
  if (sw) {
    // Value, else name — except the generic "analysis software" term with
    // no value, which is how an unknown software is written.
    software = sw.accession === "MS:1001456" && !sw.value ? null : sw.value || sw.name || null;
  }

  let softwareVersion: string | null = null;
  const settings = [...mtd.entries()]
    .map(([key, value]) => [/^software\[1\]-setting\[(\d+)\]$/.exec(key), value] as const)
    .filter((e): e is [RegExpExecArray, string] => e[0] !== null)
    .sort((a, b) => Number(a[0][1]) - Number(b[0][1]));
  for (const [, value] of settings) {
    if (value.startsWith("version=")) {
      softwareVersion = value.slice("version=".length);
      break;
    }
  }

  return createSearchInfo({
    software,
    software_version: softwareVersion,
    primary_score_name: primaryScoreName(roles.primaryParam),
    fixed_mods: parseMods(mtd, "fixed_mod"),
    variable_mods: parseMods(mtd, "variable_mod"),
  });
}

/**
 * Reader for mzTab PSM sections.
 *
 * Columns are looked up by PSH header name, so third-party mzTab files work.
 * Consecutive PSM rows sharing a `PSM_ID` collapse into one PSM whose
 * `proteins` are the rows' accessions.
 *
 * @example
 * ```typescript
 * const reader = new MzTabReader(fs.readFileSync("run.mztab"));
 * for (const psm of reader) console.log(psm.scan_number, psm.peptide);
 * const subset = reader.filter(new Set([1234, 1240]));
 * ```
 */
export class MzTabReader extends PSMCollection {
  /** MTD key/value pairs, in file order. */
  readonly metadata: Map<string, string>;
  /** The source text. */
  readonly text: string;

  /**
   * @param data - mzTab document (string or UTF-8 bytes).
   */
  constructor(data: string | Buffer) {
    const text = typeof data === "string" ? data : data.toString("utf-8");
    const mtd = new Map<string, string>();
    const psms: PSM[] = [];
    let layout: PshLayout | null = null;
    let rows: string[][] = [];

    // Header is parsed lazily: PSH must precede PSM rows, MTD may be anywhere
    // before them. Score roles depend on MTD, so rows are buffered until the end.
    const pending: Array<{ layout: PshLayout; rows: string[][] }> = [];
    for (const line of splitLines(text)) {
      if (line.trim() === "") continue;
      const fields = line.split("\t");
      const tag = fields[0];
      if (tag === "MTD") {
        if (fields.length >= 2 && !mtd.has(fields[1])) mtd.set(fields[1], fields[2] ?? "");
      } else if (tag === "PSH") {
        if (layout && rows.length > 0) pending.push({ layout, rows });
        layout = buildLayout(fields.slice(1));
        rows = [];
      } else if (tag === "PSM" && layout) {
        rows.push(fields.slice(1));
      }
    }
    if (layout && rows.length > 0) pending.push({ layout, rows });

    const roles = resolveScoreRoles(mtd);
    for (const block of pending) {
      psms.push(...parseRows(block.layout, block.rows, roles));
    }

    let sourceFormat: string | null = null;
    let sourceName: string | null = null;
    for (const [key, value] of mtd) {
      if (!/^custom\[\d+\]$/.test(key)) continue;
      const p = parseMzTabParam(value);
      if (p?.name === "source_format" && sourceFormat === null) sourceFormat = p.value || null;
      if (p?.name === "source_file" && sourceName === null) sourceName = p.value || null;
    }

    super(psms, parseSearchInfo(mtd, roles), "mztab", sourceFormat, sourceName);
    this.metadata = mtd;
    this.text = text;
  }

  /**
   * Produce an mzTab containing only the PSMs whose scan number is in
   * `scanNumbers`. Every non-PSM line is kept verbatim and line order is
   * preserved.
   *
   * @param scanNumbers - Scan numbers to keep.
   * @returns The filtered mzTab document.
   */
  filter(scanNumbers: Set<number>): string {
    let layout: PshLayout | null = null;
    const lines = this.text.split("\n");
    const hadTrailingNewline = lines.length > 0 && lines[lines.length - 1] === "";
    if (hadTrailingNewline) lines.pop();
    const out: string[] = [];
    for (const raw of lines) {
      const line = raw.endsWith("\r") ? raw.slice(0, -1) : raw;
      const fields = line.split("\t");
      if (fields[0] === "PSH") {
        layout = buildLayout(fields.slice(1));
      } else if (fields[0] === "PSM") {
        if (!layout) continue;
        const cells = fields.slice(1);
        const columns = layout.columns;
        const cell = (name: string): string | undefined => {
          const idx = columns.get(name);
          return idx === undefined ? undefined : cells[idx];
        };
        if (!scanNumbers.has(rowScanNumber(cell))) continue;
      }
      out.push(raw);
    }
    return out.join("\n") + (hadTrailingNewline ? "\n" : "");
  }
}

function parseRows(layout: PshLayout, rows: string[][], roles: ScoreRoles): PSM[] {
  const { columns, extras } = layout;
  const psmIdCol = columns.get("PSM_ID");
  const psms: PSM[] = [];
  let lastId: string | undefined;
  let current: PSM | null = null;

  for (const cells of rows) {
    const cell = (name: string): string | undefined => {
      const idx = columns.get(name);
      return idx === undefined ? undefined : cells[idx];
    };
    const accession = cell("accession");
    const id = psmIdCol === undefined ? undefined : cells[psmIdCol];

    if (current && psmIdCol !== undefined && id === lastId) {
      if (!isNull(accession) && !current.proteins.includes(accession)) {
        current.proteins.push(accession);
      }
      continue;
    }
    lastId = id;

    const ref = cell("spectra_ref") ?? "";
    const indexMatch = INDEX_REF_RE.exec(ref);
    const indexOverride = optInt(cell("opt_global_spectrum_index"));
    const scoreCell = (n: number | null): string | undefined =>
      n === null ? undefined : cell(`search_engine_score[${n}]`);

    const rtCell = cell("retention_time");
    const modifiedSequence = cell("opt_global_modified_sequence");
    const decoyCell = cell(DECOY_COLUMN);
    const legacyDecoy = cell("opt_global_decoy");

    const extra: Record<string, ExtraValue> = {};
    for (const [idx, key] of extras) {
      const value = cells[idx];
      if (isNull(value)) continue;
      extra[key] = parseExtraValue(value);
    }

    current = createPSM({
      scan_number: rowScanNumber(cell),
      peptide: cell("sequence") ?? "",
      charge: optInt(cell("charge")) ?? 0,
      score: optFloat(scoreCell(roles.primary)) ?? 0.0,
      proteins: isNull(accession) ? [] : [accession],
      spectrum_index: indexOverride ?? (indexMatch ? Number(indexMatch[1]) : null),
      q_value: optFloat(scoreCell(roles.qValue)),
      pep: optFloat(scoreCell(roles.pep)),
      retention_time: isNull(rtCell) ? null : optFloat(rtCell.split("|")[0]),
      precursor_mz: optFloat(cell("exp_mass_to_charge")),
      precursor_mass: optFloat(cell("opt_global_precursor_mass")),
      mass_diff: optFloat(cell("opt_global_mass_diff")),
      modified_peptide: isNull(modifiedSequence) ? null : modifiedSequence,
      num_matched_ions: optInt(cell("opt_global_num_matched_ions")),
      num_total_ions: optInt(cell("opt_global_num_total_ions")),
      rank: optInt(cell("opt_global_rank")) ?? 1,
      is_decoy: decoyCell === "1" || legacyDecoy === "1" || legacyDecoy === "true",
      extra,
    });
    psms.push(current);
  }
  return psms;
}
