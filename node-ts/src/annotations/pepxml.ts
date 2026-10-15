/**
 * pepXML → PSM conversion.
 *
 * Uses a small, dependency-free, namespace-agnostic tag scanner: only
 * element structure and attributes matter for pepXML, so text content is
 * ignored. Conversion rules mirror the Python `PepXMLReader` exactly so
 * both bindings produce identical PSMs from the same file.
 */

import {
  createPSM,
  createSearchInfo,
  parsePyFloat,
  parsePyInt,
  type ModificationSpec,
  type PSM,
  type SearchInfo,
} from "@/annotations/psm.js";

/** A parsed XML element (attributes and child elements only). */
export interface XmlElement {
  /** Local name (namespace prefix stripped). */
  name: string;
  /** Attributes keyed by local name. */
  attrs: Record<string, string>;
  /** Child elements, in document order. */
  children: XmlElement[];
}

/** Callbacks for {@link scanXml}. */
interface XmlHandler {
  start(name: string, attrs: Record<string, string>, selfClosing: boolean): void;
  end(name: string): void;
}

const NAMED_ENTITIES: Record<string, string> = {
  amp: "&",
  lt: "<",
  gt: ">",
  quot: '"',
  apos: "'",
};

/**
 * Decode XML entities (`&amp;` `&lt;` `&gt;` `&quot;` `&apos;` and numeric
 * references). Unknown entities are left as-is.
 *
 * @param text - Raw attribute text.
 * @returns Decoded text.
 */
export function decodeXmlEntities(text: string): string {
  if (!text.includes("&")) return text;
  return text.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][\w.-]*);/g, (whole, ent: string) => {
    if (ent[0] === "#") {
      const code = ent[1] === "x" ? parseInt(ent.slice(2), 16) : parseInt(ent.slice(1), 10);
      try {
        return String.fromCodePoint(code);
      } catch {
        return whole;
      }
    }
    return NAMED_ENTITIES[ent] ?? whole;
  });
}

function localName(qname: string): string {
  const colon = qname.lastIndexOf(":");
  return colon >= 0 ? qname.slice(colon + 1) : qname;
}

const ATTR_RE = /([^\s=/>]+)\s*=\s*(?:"([^"]*)"|'([^']*)')/g;

function parseAttrs(text: string): Record<string, string> {
  const attrs: Record<string, string> = {};
  ATTR_RE.lastIndex = 0;
  let m: RegExpExecArray | null;
  while ((m = ATTR_RE.exec(text)) !== null) {
    const qname = m[1];
    if (qname === "xmlns" || qname.startsWith("xmlns:")) continue;
    // Attribute-value normalization: literal whitespace becomes a space.
    const raw = (m[2] ?? m[3]).replace(/\r\n?/g, "\n").replace(/[\t\n]/g, " ");
    attrs[localName(qname)] = decodeXmlEntities(raw);
  }
  return attrs;
}

/**
 * Walk the element structure of an XML document, calling `handler` for each
 * start and end tag. Skips declarations, processing instructions, comments,
 * CDATA sections and DOCTYPE (including an internal subset).
 */
function scanXml(xml: string, handler: XmlHandler): void {
  const len = xml.length;
  let pos = xml.indexOf("<");
  while (pos >= 0 && pos < len) {
    const next = xml.charCodeAt(pos + 1);
    if (next === 63 /* ? */) {
      const close = xml.indexOf("?>", pos + 2);
      if (close < 0) break;
      pos = xml.indexOf("<", close + 2);
      continue;
    }
    if (next === 33 /* ! */) {
      let close: number;
      if (xml.startsWith("<!--", pos)) {
        close = xml.indexOf("-->", pos + 4);
        if (close < 0) break;
        close += 3;
      } else if (xml.startsWith("<![CDATA[", pos)) {
        close = xml.indexOf("]]>", pos + 9);
        if (close < 0) break;
        close += 3;
      } else {
        // DOCTYPE / other declarations; honor an internal [...] subset.
        let depth = 0;
        close = -1;
        for (let i = pos + 2; i < len; i++) {
          const c = xml[i];
          if (c === "[") depth++;
          else if (c === "]") depth--;
          else if (c === ">" && depth <= 0) {
            close = i + 1;
            break;
          }
        }
        if (close < 0) break;
      }
      pos = xml.indexOf("<", close);
      continue;
    }
    if (next === 47 /* / */) {
      const close = xml.indexOf(">", pos + 2);
      if (close < 0) break;
      handler.end(localName(xml.slice(pos + 2, close).trim()));
      pos = xml.indexOf("<", close + 1);
      continue;
    }

    // Start tag: find the closing '>' outside quoted attribute values.
    let quote = 0;
    let close = -1;
    for (let i = pos + 1; i < len; i++) {
      const c = xml.charCodeAt(i);
      if (quote) {
        if (c === quote) quote = 0;
      } else if (c === 34 || c === 39) {
        quote = c;
      } else if (c === 62 /* > */) {
        close = i;
        break;
      }
    }
    if (close < 0) break;
    let body = xml.slice(pos + 1, close);
    const selfClosing = body.endsWith("/");
    if (selfClosing) body = body.slice(0, -1);
    const nameEnd = body.search(/[\s]/);
    const qname = nameEnd < 0 ? body : body.slice(0, nameEnd);
    const name = localName(qname);
    handler.start(name, nameEnd < 0 ? {} : parseAttrs(body.slice(nameEnd)), selfClosing);
    if (selfClosing) handler.end(name);
    pos = xml.indexOf("<", close + 1);
  }
}

/**
 * Stream an XML document, materializing a lightweight element tree for each
 * element whose local name is in `capture` (nested captures are not split
 * out). Each captured subtree is passed to `onElement` once complete.
 *
 * @param xml - XML document text.
 * @param capture - Local names of elements to materialize.
 * @param onElement - Receives each completed captured element.
 */
export function collectXmlElements(
  xml: string,
  capture: Set<string>,
  onElement: (element: XmlElement) => void
): void {
  const stack: XmlElement[] = [];
  scanXml(xml, {
    start(name, attrs) {
      if (stack.length === 0 && !capture.has(name)) return;
      const el: XmlElement = { name, attrs, children: [] };
      if (stack.length > 0) stack[stack.length - 1].children.push(el);
      stack.push(el);
    },
    end() {
      if (stack.length === 0) return;
      const el = stack.pop() as XmlElement;
      if (stack.length === 0) onElement(el);
    },
  });
}

/** Yield `el` and all its descendants in document order (like ElementTree `iter()`). */
function* iterElements(el: XmlElement): Generator<XmlElement> {
  yield el;
  for (const child of el.children) yield* iterElements(child);
}

/** Options for {@link parsePepXML}. */
export interface PepXMLOptions {
  /** Only include hits with `hit_rank <= minRank` (default 1). */
  minRank?: number;
  /** Substring identifying decoy proteins (default `"DECOY_"`). */
  decoyPrefix?: string;
}

/** Python `float(attr)` with a default when the attribute is missing; throws on bad text. */
function floatAttr(el: XmlElement, name: string, fallback: number): number {
  const raw = el.attrs[name];
  if (raw === undefined) return fallback;
  const value = parsePyFloat(raw);
  if (value === null) throw new Error(`invalid float for ${name}: '${raw}'`);
  return value;
}

/** Python `int(attr)` with a default when the attribute is missing; throws on bad text. */
function intAttr(el: XmlElement, name: string, fallback: number): number {
  const raw = el.attrs[name];
  if (raw === undefined) return fallback;
  const value = parsePyInt(raw);
  if (value === null) throw new Error(`invalid integer for ${name}: '${raw}'`);
  return value;
}

function parseSearchSummary(el: XmlElement): SearchInfo {
  const fixed: ModificationSpec[] = [];
  const variable: ModificationSpec[] = [];
  for (const child of el.children) {
    const massdiff = parsePyFloat(child.attrs.massdiff ?? "");
    if (massdiff === null) continue;
    const isVariable = (child.attrs.variable ?? "N").toUpperCase() === "Y";
    let mod: ModificationSpec;
    if (child.name === "aminoacid_modification") {
      mod = { massdiff, site: child.attrs.aminoacid ?? "", position: "Anywhere" };
    } else if (child.name === "terminal_modification") {
      const end = (child.attrs.terminus ?? "n").toUpperCase() === "N" ? "N" : "C";
      const scope = (child.attrs.protein_terminus ?? "N").toUpperCase() === "Y" ? "Protein" : "Any";
      mod = { massdiff, site: `${end}-term`, position: `${scope} ${end}-term` };
    } else {
      continue;
    }
    (isVariable ? variable : fixed).push(mod);
  }
  return createSearchInfo({
    software: el.attrs.search_engine || null,
    software_version: el.attrs.search_engine_version || null,
    fixed_mods: fixed,
    variable_mods: variable,
  });
}

function parseSearchHit(
  hit: XmlElement,
  base: Pick<
    PSM,
    "scan_number" | "charge" | "precursor_mz" | "precursor_mass" | "retention_time" | "rank"
  >,
  decoyPrefix: string
): PSM | null {
  try {
    const peptide = hit.attrs.peptide ?? "";
    const protein = hit.attrs.protein ?? "";
    const proteins = protein ? [protein] : [];

    let modifiedPeptide: string | null = null;
    const scores: Record<string, number> = {};
    let pep: number | null = null;
    for (const el of iterElements(hit)) {
      if (el === hit) continue;
      if (el.name === "alternative_protein") {
        const alt = el.attrs.protein ?? "";
        if (alt && !proteins.includes(alt)) proteins.push(alt);
      }
    }

    const calcMass = floatAttr(hit, "calc_neutral_pep_mass", 0);
    const massDiff = (base.precursor_mass as number) - calcMass;
    const numMatched = intAttr(hit, "num_matched_ions", 0);
    const numTotal = intAttr(hit, "tot_num_ions", 0);
    const isDecoy = proteins.some((p) => p.includes(decoyPrefix));

    for (const el of iterElements(hit)) {
      if (el.name === "modification_info") {
        modifiedPeptide = el.attrs.modified_peptide ?? peptide;
        break;
      }
    }
    for (const el of iterElements(hit)) {
      if (el.name === "search_score") {
        const value = parsePyFloat(el.attrs.value ?? "0");
        if (value !== null) scores[el.attrs.name ?? ""] = value;
      }
    }
    // PeptideProphet probability → PEP. Python visits each result once as a
    // descendant and again as a child of analysis_result; last one wins.
    for (const el of iterElements(hit)) {
      if (el.name === "peptideprophet_result") {
        pep = 1.0 - floatAttr(el, "probability", 0);
      } else if (el.name === "analysis_result") {
        for (const child of el.children) {
          if (child.name === "peptideprophet_result") {
            pep = 1.0 - floatAttr(child, "probability", 0);
          }
        }
      }
    }

    let score = scores.hyperscore ?? scores.xcorr ?? scores.expect ?? 0;
    const names = Object.keys(scores);
    if (score === 0 && names.length > 0) score = scores[names[0]];

    return createPSM({
      ...base,
      peptide,
      score,
      proteins,
      mass_diff: massDiff,
      modified_peptide: modifiedPeptide ?? peptide,
      num_matched_ions: numMatched,
      num_total_ions: numTotal,
      q_value: null,
      pep,
      is_decoy: isDecoy,
      extra: scores,
    });
  } catch {
    return null;
  }
}

function parseSpectrumQuery(query: XmlElement, minRank: number, decoyPrefix: string): PSM[] {
  const spectrum = query.attrs.spectrum ?? "";
  const scanMatch = /\.(\d+)\./.exec(spectrum);
  const scanNumber = scanMatch ? Number(scanMatch[1]) : intAttr(query, "start_scan", 0);
  const precursorMass = floatAttr(query, "precursor_neutral_mass", 0);
  const charge = intAttr(query, "assumed_charge", 2);
  const precursorMz = precursorMass / (charge + 1.00727646677);
  const retentionTime = floatAttr(query, "retention_time_sec", 0);

  const psms: PSM[] = [];
  for (const result of iterElements(query)) {
    if (result.name !== "search_result") continue;
    for (const hit of iterElements(result)) {
      if (hit.name !== "search_hit") continue;
      const rank = intAttr(hit, "hit_rank", 1);
      if (rank > minRank) continue;
      const psm = parseSearchHit(
        hit,
        {
          scan_number: scanNumber,
          charge,
          precursor_mz: precursorMz,
          precursor_mass: precursorMass,
          retention_time: retentionTime,
          rank,
        },
        decoyPrefix
      );
      if (psm) psms.push(psm);
    }
  }
  return psms;
}

/**
 * Convert a pepXML document into PSMs plus search metadata.
 *
 * @param data - pepXML text or UTF-8 bytes.
 * @param options - Rank cutoff and decoy prefix.
 * @returns PSMs (one per kept `search_hit`) and the first `search_summary`.
 * @throws Error if a `spectrum_query` carries malformed numeric attributes.
 */
export function parsePepXML(
  data: string | Buffer,
  options?: PepXMLOptions
): { psms: PSM[]; searchInfo: SearchInfo } {
  const xml = typeof data === "string" ? data : data.toString("utf-8");
  const minRank = options?.minRank ?? 1;
  const decoyPrefix = options?.decoyPrefix ?? "DECOY_";
  const psms: PSM[] = [];
  let searchInfo: SearchInfo | null = null;

  collectXmlElements(xml, new Set(["search_summary", "spectrum_query"]), (el) => {
    if (el.name === "search_summary") {
      if (searchInfo === null) searchInfo = parseSearchSummary(el);
    } else {
      psms.push(...parseSpectrumQuery(el, minRank, decoyPrefix));
    }
  });

  const info: SearchInfo = searchInfo ?? createSearchInfo();
  if (psms.length > 0 && info.primary_score_name === null) {
    const scores = psms[0].extra;
    const preferred = ["hyperscore", "xcorr", "expect"].find((n) => n in scores);
    info.primary_score_name = preferred ?? Object.keys(scores)[0] ?? null;
  }
  return { psms, searchInfo: info };
}
