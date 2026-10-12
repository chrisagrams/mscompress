/**
 * mzTab as the sole stored PSM format of MSZX archives.
 *
 * Covers the mzTab writer/reader, pepXML and Percolator conversion, the
 * builder / batch writer storing only mzTab, scan-filtered extraction, the
 * read-only legacy path for archives holding engine-specific annotation
 * members, and cross-language conformance against Python-written fixtures.
 */

import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as tar from "tar";
import native from "../src/core/bindings.js";
import {
  MSZFile,
  MSZXFile,
  MSZXBuilder,
  MSZXBatchWriter,
  MzTabReader,
  PSMCollection,
  createPSM,
  createSearchInfo,
  createMSZX,
  parsePepXML,
  parsePercolator,
  readPSMs,
  toMzTab,
  writeMzTab,
  type PSM,
  type SearchInfo,
} from "../src/index.js";
import { MSZ_PATH, MSZX_PATH, MZML_PATH, TEST_DATA_DIR } from "./fixtures.js";

const PEPXML_PATH = path.join(TEST_DATA_DIR, "pepXML", "test.pepXML");
const TSV_PATH = path.join(TEST_DATA_DIR, "percolator", "test.tsv");
const MZTAB_FIXTURE_DIR = path.join(TEST_DATA_DIR, "mztab");

let tmpDir: string;

beforeAll(() => {
  tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mztab-test-"));
});

afterAll(() => {
  if (tmpDir) fs.rmSync(tmpDir, { recursive: true, force: true });
});

/** Rows of one section, split into cells (tag included). */
function rows(text: string, tag: string): string[][] {
  return text
    .split("\n")
    .filter((l) => l.startsWith(`${tag}\t`))
    .map((l) => l.split("\t"));
}

/** All tar members of an archive, keyed by name. */
function tarMembers(file: string): Map<string, Buffer> {
  const members = new Map<string, Buffer>();
  tar.list({
    file,
    sync: true,
    onReadEntry: (entry) => {
      const chunks: Buffer[] = [];
      entry.on("data", (c: Buffer) => chunks.push(c));
      entry.on("end", () => members.set(entry.path, Buffer.concat(chunks)));
    },
  });
  return members;
}

/** Floats equal within a relative tolerance of 1e-12; NaN equals NaN. */
function closeEnough(a: unknown, b: unknown): boolean {
  if (typeof a === "number" && typeof b === "number") {
    if (Number.isNaN(a) || Number.isNaN(b)) return Number.isNaN(a) && Number.isNaN(b);
    if (a === b) return true;
    return Math.abs(a - b) <= 1e-12 * Math.max(Math.abs(a), Math.abs(b));
  }
  if (Array.isArray(a) && Array.isArray(b)) {
    return a.length === b.length && a.every((x, i) => closeEnough(x, b[i]));
  }
  if (a && b && typeof a === "object" && typeof b === "object") {
    const ka = Object.keys(a).sort();
    const kb = Object.keys(b).sort();
    return (
      closeEnough(ka, kb) &&
      ka.every((k) =>
        closeEnough((a as Record<string, unknown>)[k], (b as Record<string, unknown>)[k])
      )
    );
  }
  return a === b;
}

/** Assert two PSM lists match field-by-field, reporting the first mismatch. */
function expectPSMsEqual(actual: PSM[], expected: PSM[]): void {
  expect(actual.length).toBe(expected.length);
  for (let i = 0; i < expected.length; i++) {
    for (const key of Object.keys(expected[i]) as (keyof PSM)[]) {
      if (!closeEnough(actual[i][key], expected[i][key])) {
        expect({ index: i, field: key, value: actual[i][key] }).toEqual({
          index: i,
          field: key,
          value: expected[i][key],
        });
      }
    }
  }
}

function expectSearchInfoEqual(actual: SearchInfo, expected: SearchInfo): void {
  expect(closeEnough(actual, expected)).toBe(true);
}

describe("writeMzTab", () => {
  const info = createSearchInfo({
    software: "Comet",
    software_version: "2024.01",
    primary_score_name: "xcorr",
    fixed_mods: [{ massdiff: 57.021464, site: "C", position: "Anywhere" }],
    variable_mods: [{ massdiff: -17.026549, site: "N-term", position: "Any N-term" }],
  });
  const psms: PSM[] = [
    createPSM({
      scan_number: 10,
      peptide: "PEPTIDE",
      charge: 2,
      score: 3.5,
      proteins: ["P1", "P2"],
      q_value: 0.01,
      retention_time: 12.5,
      extra: { "delta cn": 0.25, engine: "x", flag: true },
    }),
    createPSM({
      scan_number: 11,
      peptide: "DECOYPEP",
      charge: 3,
      score: Number.NaN,
      spectrum_index: 4,
      is_decoy: true,
      rank: 2,
      extra: { other: 1 },
    }),
  ];
  const text = writeMzTab(psms, {
    searchInfo: info,
    msRunLocation: "run.msz",
    sourceFormat: "pepxml",
    sourceName: "run.pepXML",
    title: "my title",
  });

  it("emits MTD lines in the specified order", () => {
    expect(rows(text, "MTD").map((r) => `${r[1]}\t${r[2]}`)).toEqual([
      "mzTab-version\t1.0.0",
      "mzTab-mode\tSummary",
      "mzTab-type\tIdentification",
      "mzTab-ID\tmy title",
      "description\tConverted by mscompress from pepxml (run.pepXML)",
      "ms_run[1]-location\tfile://run.msz",
      "software[1]\t[MS, MS:1001456, analysis software, Comet]",
      "software[1]-setting[1]\tversion=2024.01",
      "psm_search_engine_score[1]\t[MS, MS:1001155, SEQUEST:xcorr, ]",
      "psm_search_engine_score[2]\t[MS, MS:1002354, PSM-level q-value, ]",
      "psm_search_engine_score[3]\t[, , posterior error probability, ]",
      "fixed_mod[1]\t[CHEMMOD, CHEMMOD:+57.021464, , ]",
      "fixed_mod[1]-site\tC",
      "fixed_mod[1]-position\tAnywhere",
      "variable_mod[1]\t[CHEMMOD, CHEMMOD:-17.026549, , ]",
      "variable_mod[1]-site\tN-term",
      "variable_mod[1]-position\tAny N-term",
      "custom[1]\t[, , source_format, pepxml]",
      "custom[2]\t[, , source_file, run.pepXML]",
    ]);
  });

  it("writes the fixed PSH columns followed by extra columns", () => {
    const psh = rows(text, "PSH");
    expect(psh.length).toBe(1);
    expect(psh[0].slice(1, 3)).toEqual(["sequence", "PSM_ID"]);
    expect(psh[0].slice(21, 30)).toEqual([
      "opt_global_cv_MS:1002217_decoy_peptide",
      "opt_global_modified_sequence",
      "opt_global_rank",
      "opt_global_scan_number",
      "opt_global_spectrum_index",
      "opt_global_precursor_mass",
      "opt_global_mass_diff",
      "opt_global_num_matched_ions",
      "opt_global_num_total_ions",
    ]);
    expect(psh[0].slice(30)).toEqual([
      "opt_global_extra_delta_cn",
      "opt_global_extra_engine",
      "opt_global_extra_flag",
      "opt_global_extra_other",
    ]);
  });

  it("writes one row per protein sharing a PSM_ID, and null placeholders", () => {
    const header = rows(text, "PSH")[0];
    const col = (r: string[], name: string) => r[header.indexOf(name)];
    const psm = rows(text, "PSM");
    expect(psm.length).toBe(3);
    expect(psm.map((r) => col(r, "PSM_ID"))).toEqual(["1", "1", "2"]);
    expect(psm.map((r) => col(r, "accession"))).toEqual(["P1", "P2", "null"]);
    expect(psm.map((r) => col(r, "unique"))).toEqual(["0", "0", "null"]);
    expect(col(psm[0], "spectra_ref")).toBe("ms_run[1]:scan=10");
    expect(col(psm[2], "spectra_ref")).toBe("ms_run[1]:index=4");
    expect(col(psm[2], "search_engine_score[1]")).toBe("NaN");
    expect(col(psm[2], "search_engine_score[2]")).toBe("null");
    expect(col(psm[2], "opt_global_cv_MS:1002217_decoy_peptide")).toBe("1");
    expect(col(psm[2], "opt_global_extra_engine")).toBe("null");
    expect(col(psm[0], "opt_global_extra_flag")).toBe("true");
  });

  it("round-trips through MzTabReader", () => {
    const reader = new MzTabReader(text);
    expect(reader.sourceFormat).toBe("pepxml");
    expect(reader.sourceName).toBe("run.pepXML");
    expectSearchInfoEqual(reader.searchInfo, info);
    // Whitespace in extra keys is normalized to "_" on write.
    const expected = psms.map((p) => ({ ...p }));
    expected[0].extra = { delta_cn: 0.25, engine: "x", flag: true };
    expectPSMsEqual(reader.psms, expected);
  });

  it("uses the generic description and placeholders without metadata", () => {
    const bare = writeMzTab([]);
    const mtd = new Map(rows(bare, "MTD").map((r) => [r[1], r[2]]));
    expect(mtd.get("description")).toBe("PSMs written by mscompress");
    expect(mtd.get("ms_run[1]-location")).toBe("null");
    expect(mtd.get("software[1]")).toBe("[MS, MS:1001456, analysis software, ]");
    expect(mtd.get("fixed_mod[1]")).toBe("[MS, MS:1002453, No fixed modifications searched, ]");
    expect(mtd.has("custom[1]")).toBe(false);
    const reader = new MzTabReader(bare);
    expect(reader.length).toBe(0);
    expect(reader.searchInfo.fixed_mods).toEqual([]);
  });
});

describe("MzTabReader", () => {
  // Third-party style: different column order, no mscompress opt columns,
  // q-value as score[1], comments, PRT section, CRLF line endings.
  const thirdParty = [
    "COM\tsome comment",
    "MTD\tmzTab-version\t1.0.0",
    "MTD\tmzTab-mode\tSummary",
    "MTD\tmzTab-type\tIdentification",
    "MTD\tsoftware[1]\t[MS, MS:1001476, X!Tandem, ]",
    "MTD\tpsm_search_engine_score[1]\t[MS, MS:1002354, PSM-level q-value, ]",
    "MTD\tpsm_search_engine_score[2]\t[MS, MS:1001330, X!Tandem:expect, ]",
    "MTD\tvariable_mod[1]\t[UNIMOD, UNIMOD:35, Oxidation, ]",
    "",
    "PRH\taccession\tdescription",
    "PRT\tP1\tprotein",
    "",
    "PSH\tPSM_ID\tsequence\taccession\tcharge\tretention_time\tspectra_ref\tsearch_engine_score[1]\tsearch_engine_score[2]\topt_global_decoy\topt_global_foo",
    "PSM\t7\tAAA\tP1\t2\t10.5|11.0\tms_run[1]:controllerType=0 controllerNumber=1 scan=42\t0.001\t1e-05\ttrue\tbar",
    "PSM\t7\tAAA\tP2\t2\t10.5|11.0\tms_run[1]:controllerType=0 controllerNumber=1 scan=42\t0.001\t1e-05\ttrue\tbar",
    "PSM\t8\tCCC\tnull\tnull\tnull\tms_run[1]:index=3\tnull\tnull\tnull\t2.5",
    "",
  ].join("\r\n");

  it("parses columns by header name", () => {
    const reader = new MzTabReader(thirdParty);
    expect(reader.length).toBe(2);
    const [a, b] = reader.psms;
    expect(a.scan_number).toBe(42);
    expect(a.peptide).toBe("AAA");
    expect(a.proteins).toEqual(["P1", "P2"]);
    expect(a.charge).toBe(2);
    expect(a.retention_time).toBe(10.5);
    expect(a.score).toBe(1e-5);
    expect(a.q_value).toBe(0.001);
    expect(a.pep).toBeNull();
    expect(a.is_decoy).toBe(true);
    expect(a.extra).toEqual({ foo: "bar" });
    expect(b.scan_number).toBe(0);
    expect(b.spectrum_index).toBe(3);
    expect(b.charge).toBe(0);
    expect(b.score).toBe(0);
    expect(b.proteins).toEqual([]);
    expect(b.extra).toEqual({ foo: 2.5 });
    expect(b.rank).toBe(1);
  });

  it("reads search info from the MTD section", () => {
    const info = new MzTabReader(thirdParty).searchInfo;
    expect(info.software).toBe("X!Tandem");
    expect(info.primary_score_name).toBe("expect");
    expect(info.variable_mods).toEqual([]); // non-CHEMMOD mods are skipped
  });

  it("supports scan lookups", () => {
    const reader = new MzTabReader(thirdParty);
    expect(reader.hasScan(42)).toBe(true);
    expect(reader.hasScan(43)).toBe(false);
    expect(reader.getByScan(42).length).toBe(1);
    expect(reader.getBestByScan(42)?.peptide).toBe("AAA");
    expect(reader.getBestByScan(43)).toBeNull();
    expect(reader.scanNumbers).toEqual([42, 0]);
    expect([...reader].length).toBe(2);
  });

  it("filters PSM rows by scan, keeping every other line verbatim", () => {
    const text = toMzTab(fs.readFileSync(TSV_PATH), TSV_PATH);
    const reader = new MzTabReader(text);
    const keep = new Set(reader.scanNumbers.slice(0, 3));
    const filtered = reader.filter(keep);

    const nonPsm = (t: string) => t.split("\n").filter((l) => !l.startsWith("PSM\t"));
    expect(nonPsm(filtered)).toEqual(nonPsm(text));
    const kept = text.split("\n").filter((l) => filtered.split("\n").includes(l));
    expect(filtered.split("\n")).toEqual(kept); // order preserved

    const sub = new MzTabReader(filtered);
    expect(new Set(sub.scanNumbers)).toEqual(keep);
    expectPSMsEqual(
      sub.psms,
      reader.psms.filter((p) => keep.has(p.scan_number))
    );
    expect(new MzTabReader(reader.filter(new Set())).length).toBe(0);
  });
});

describe("engine conversion round trips", () => {
  it("pepXML → mzTab → PSMs matches direct conversion", () => {
    const direct = parsePepXML(fs.readFileSync(PEPXML_PATH));
    expect(direct.psms.length).toBe(100);
    expect(direct.searchInfo.software).toBe("X! Tandem");
    expect(direct.searchInfo.primary_score_name).toBe("hyperscore");

    const reader = new MzTabReader(toMzTab(fs.readFileSync(PEPXML_PATH), PEPXML_PATH));
    expect(reader.sourceFormat).toBe("pepxml");
    expect(reader.sourceName).toBe("test.pepXML");
    expectPSMsEqual(reader.psms, direct.psms);
    expectSearchInfoEqual(reader.searchInfo, direct.searchInfo);
  });

  it("Percolator TSV → mzTab → PSMs matches direct conversion", () => {
    const direct = parsePercolator(fs.readFileSync(TSV_PATH));
    expect(direct.psms.length).toBe(100);
    expect(direct.searchInfo.software).toBe("Percolator");
    expect(direct.psms[0].q_value).toBeCloseTo(0.277778);

    const reader = new MzTabReader(toMzTab(fs.readFileSync(TSV_PATH), TSV_PATH));
    expect(reader.sourceFormat).toBe("percolator_tsv");
    expectPSMsEqual(reader.psms, direct.psms);
    expectSearchInfoEqual(reader.searchInfo, direct.searchInfo);
  });

  it("readPSMs auto-detects by name and by content", () => {
    const pep = fs.readFileSync(PEPXML_PATH);
    expect(readPSMs(pep, "x.pep.xml").format).toBe("pepxml");
    expect(readPSMs(pep).format).toBe("pepxml");
    expect(readPSMs(native.zstdCompress(pep), "x.pepXML.zst").format).toBe("pepxml");
    expect(readPSMs(fs.readFileSync(TSV_PATH)).format).toBe("percolator_tsv");
    expect(readPSMs(fs.readFileSync(TSV_PATH), "a.pin", "tsv").format).toBe("percolator_tsv");
    const mztab = toMzTab(pep, "x.pepXML");
    const r = readPSMs(mztab);
    expect(r).toBeInstanceOf(MzTabReader);
    expect(toMzTab(mztab, "x.mztab")).toBe(mztab); // passthrough
    expect(() => readPSMs("hello world")).toThrow("Could not determine");
  });

  it("parses namespaced, entity-bearing pepXML", () => {
    const xml = `<?xml version="1.0"?>
<!-- comment <spectrum_query> -->
<!DOCTYPE msms_pipeline_analysis [ <!ENTITY foo "bar"> ]>
<p:msms_pipeline_analysis xmlns:p="http://regis-web.systemsbiology.net/pepXML">
 <p:msms_run_summary>
  <p:search_summary search_engine='Comet &amp; Co' search_engine_version="1">
   <p:terminal_modification terminus="c" massdiff="-1.5" variable="N" protein_terminus="N"/>
  </p:search_summary>
  <p:spectrum_query spectrum="f.00012.00012.3" precursor_neutral_mass="1000.5" assumed_charge="3">
   <p:search_result>
    <p:search_hit hit_rank="1" peptide="PEP" protein="DECOY_&lt;P&gt;" calc_neutral_pep_mass="1000">
     <p:alternative_protein protein="Q&#x41;&#66;"/>
     <p:search_score name="xcorr" value="2.5"/>
     <p:search_score name="bad" value="n/a"/>
     <p:analysis_result><p:peptideprophet_result probability="0.75"/></p:analysis_result>
    </p:search_hit>
    <p:search_hit hit_rank="2" peptide="NOPE"/>
   </p:search_result>
  </p:spectrum_query>
 </p:msms_run_summary>
</p:msms_pipeline_analysis>`;
    const { psms, searchInfo } = parsePepXML(xml);
    expect(searchInfo.software).toBe("Comet & Co");
    expect(searchInfo.fixed_mods).toEqual([
      { massdiff: -1.5, site: "C-term", position: "Any C-term" },
    ]);
    expect(searchInfo.primary_score_name).toBe("xcorr");
    expect(psms.length).toBe(1);
    expect(psms[0].scan_number).toBe(12);
    expect(psms[0].charge).toBe(3);
    expect(psms[0].proteins).toEqual(["DECOY_<P>", "QAB"]);
    expect(psms[0].is_decoy).toBe(true);
    expect(psms[0].score).toBe(2.5);
    expect(psms[0].pep).toBeCloseTo(0.25);
    expect(psms[0].mass_diff).toBeCloseTo(0.5);
    expect(psms[0].modified_peptide).toBe("PEP");
  });
});

describe("cross-language fixtures (Python-written)", () => {
  const cases = [
    { name: "pepXML", source: PEPXML_PATH, stem: "test.pepXML" },
    { name: "Percolator TSV", source: TSV_PATH, stem: "test.tsv" },
  ];

  for (const c of cases) {
    const mztabPath = path.join(MZTAB_FIXTURE_DIR, `${c.stem}.mztab`);
    const jsonPath = path.join(MZTAB_FIXTURE_DIR, `${c.stem}.psms.json`);
    const available = fs.existsSync(mztabPath) && fs.existsSync(jsonPath);
    const run = available ? it : it.skip;

    const load = (): { search_info: SearchInfo; psms: PSM[] } =>
      JSON.parse(fs.readFileSync(jsonPath, "utf-8")) as { search_info: SearchInfo; psms: PSM[] };

    run(`${c.name}: Node parses the Python mzTab into the Python PSMs`, () => {
      const expected = load();
      const reader = new MzTabReader(fs.readFileSync(mztabPath));
      expectPSMsEqual(reader.psms, expected.psms);
      expectSearchInfoEqual(reader.searchInfo, expected.search_info);
    });

    run(`${c.name}: Node converts the source into the Python PSMs`, () => {
      const expected = load();
      const converted = readPSMs(fs.readFileSync(c.source), c.source);
      expectPSMsEqual(converted.psms, expected.psms);
      expectSearchInfoEqual(converted.searchInfo, expected.search_info);
    });
  }
});

describe("MSZXBuilder stores annotations as mzTab", () => {
  let msz: MSZFile;
  let archive: string;

  beforeAll(async () => {
    msz = new MSZFile(MSZ_PATH);
    archive = path.join(tmpDir, "built.mszx");
    const builder = new MSZXBuilder(msz);
    builder.addAnnotations(PEPXML_PATH, { compressed: true, description: "search" });
    builder.addAnnotations(TSV_PATH, { compressed: true, format: "percolator_tsv" });
    builder.addPSMs([createPSM({ scan_number: 1, peptide: "AAA", charge: 2, score: 1 })], {
      filename: "manual",
      compressed: true,
    });
    await builder.save(archive);
  });

  afterAll(() => msz?.close());

  it("writes only .mztab.zst members with format mztab", () => {
    const members = [...tarMembers(archive).keys()].sort();
    expect(members).toEqual(
      [
        "manifest.json",
        path.basename(MSZ_PATH),
        "manual.mztab.zst",
        "test.pepXML.mztab.zst",
        "test.tsv.mztab.zst",
      ].sort()
    );
    const file = MSZXFile.open(archive);
    try {
      expect(file.manifest.join_key).toBe("spectra_ref");
      expect(file.manifest.annotations.map((a) => [a.filename, a.format, a.compressed])).toEqual([
        ["test.pepXML.mztab.zst", "mztab", true],
        ["test.tsv.mztab.zst", "mztab", true],
        ["manual.mztab.zst", "mztab", true],
      ]);
      expect(file.manifest.annotations.map((a) => a.num_records)).toEqual([100, 100, 1]);
      expect(file.manifest.annotations[0].description).toBe("search");
      const text = file.getAnnotationFile("test.pepXML.mztab.zst").toString("utf-8");
      expect(text).toContain(`MTD\tms_run[1]-location\tfile://${path.basename(MSZ_PATH)}`);
    } finally {
      file.close();
    }
  });

  it("exposes PSMs through MSZXFile", () => {
    const file = MSZXFile.open(archive);
    try {
      expect(file.annotationReader).toBeInstanceOf(MzTabReader);
      expect(file.psms.length).toBe(100);
      expect(file.psmReaders.size).toBe(3);
      const tsv = file.getPSMReader("test.tsv.mztab.zst");
      expect(tsv.sourceFormat).toBe("percolator_tsv");
      expectPSMsEqual(tsv.psms, parsePercolator(fs.readFileSync(TSV_PATH)).psms);
      expect(file.getPSMReader("manual.mztab.zst").psms[0].peptide).toBe("AAA");
    } finally {
      file.close();
    }
  });

  it("stores zstd-compressed mzTab by default, like Python (createMSZX)", async () => {
    const out = path.join(tmpDir, "default.mszx");
    await createMSZX(msz, out, { annotations: [TSV_PATH] });
    const members = tarMembers(out);
    expect([...members.keys()].filter((n) => n.includes("mztab"))).toEqual(["test.tsv.mztab.zst"]);
    const text = native.zstdDecompress(members.get("test.tsv.mztab.zst") as Buffer);
    expect(text.toString("utf-8").startsWith("MTD\t")).toBe(true);
  });

  it("stores plain mzTab with compressed: false", async () => {
    const out = path.join(tmpDir, "plain.mszx");
    await new MSZXBuilder(msz).addAnnotations(TSV_PATH, { compressed: false }).save(out);
    const members = tarMembers(out);
    expect(members.get("test.tsv.mztab")?.toString("utf-8").startsWith("MTD\t")).toBe(true);
  });
});

describe("legacy archives (engine-specific annotation members)", () => {
  it("opens test.mszx and queries its pepXML annotation", () => {
    const file = MSZXFile.open(MSZX_PATH);
    try {
      expect(file.annotation_files[0].format).toBe("pepxml");
      const reader = file.getPSMReader("HSA.pepXML.zst");
      expect(reader).toBeInstanceOf(PSMCollection);
      expect(reader.format).toBe("pepxml");
      expect(reader.length).toBe(100);
      expect(file.annotationReader).toBe(reader);
      expect(file.psms.length).toBe(100);
      const scan = file.positions.scans[0];
      expect(reader.hasScan(scan)).toBe(true);
      expect(reader.getBestByScan(scan)?.peptide).toBeTruthy();
    } finally {
      file.close();
    }
  });

  it("extract to .mszx converts and filters annotations to the extracted scans", () => {
    const file = MSZXFile.open(MSZX_PATH);
    const out = path.join(tmpDir, "legacy-extract.mszx");
    let extracted: MSZXFile | null = null;
    let twice: MSZXFile | null = null;
    try {
      const scans = Array.from(file.positions.scans.slice(0, 5));
      const allPsms = file.getPSMReader("HSA.pepXML.zst").psms;
      extracted = file.extract(out, { scanNumbers: scans }) as MSZXFile;
      expect(extracted.spectra.length).toBe(5);
      expect(extracted.manifest.annotations.map((a) => [a.filename, a.format])).toEqual([
        ["HSA.pepXML.mztab.zst", "mztab"],
      ]);
      const reader = extracted.annotationReader as MzTabReader;
      expect(reader).toBeInstanceOf(MzTabReader);
      expect(reader.sourceFormat).toBe("pepxml");
      expect(reader.sourceName).toBe("HSA.pepXML");
      const expected = allPsms.filter((p) => scans.includes(p.scan_number));
      expect(expected.length).toBeGreaterThan(0);
      expectPSMsEqual(reader.psms, expected);
      expect(extracted.manifest.annotations[0].num_records).toBe(expected.length);

      // Extracting again goes through the mzTab filter path.
      const out2 = path.join(tmpDir, "legacy-extract-2.mszx");
      twice = extracted.extract(out2, { indices: [0, 1] }) as MSZXFile;
      const keep = new Set(Array.from(extracted.positions.scans.slice(0, 2)));
      expect(twice.manifest.annotations[0].filename).toBe("HSA.pepXML.mztab.zst");
      expectPSMsEqual(
        twice.psms,
        expected.filter((p) => keep.has(p.scan_number))
      );
    } finally {
      twice?.close();
      extracted?.close();
      file.close();
    }
  });
});

describe("MSZXBatchWriter.addPSMs", () => {
  it("converts to compressed mzTab and records it in the manifest", async () => {
    const out = path.join(tmpDir, "batch-psms.mszx");
    const writer = new MSZXBatchWriter(out);
    let names: string[];
    try {
      const idx = await writer.add(MZML_PATH);
      names = [
        writer.addPSMs(idx, PEPXML_PATH),
        writer.addPSMs(idx, fs.readFileSync(TSV_PATH), { nameHint: "run.pin", compressed: false }),
      ];
      writer.finish();
    } catch (err) {
      writer.abort();
      throw err;
    }
    expect(names).toEqual(["test.pepXML.mztab.zst", "run.pin.mztab"]);

    const members = tarMembers(out);
    const manifest = JSON.parse(members.get("manifest.json")?.toString("utf-8") ?? "{}") as {
      spectra_files: Array<{
        annotations: Array<{
          filename: string;
          format: string;
          compressed: boolean;
          num_records: number;
        }>;
      }>;
    };
    const anns = manifest.spectra_files[0].annotations;
    expect(anns.map((a) => [a.filename, a.format, a.compressed, a.num_records])).toEqual([
      ["test.pepXML.mztab.zst", "mztab", true, 100],
      ["run.pin.mztab", "mztab", false, 100],
    ]);
    const pep = native.zstdDecompress(members.get("test.pepXML.mztab.zst") as Buffer);
    const reader = new MzTabReader(pep);
    // Source mzML name, matching the Python batch writer.
    expect(reader.metadata.get("ms_run[1]-location")).toBe("file://test.mzML");
    expectPSMsEqual(reader.psms, parsePepXML(fs.readFileSync(PEPXML_PATH)).psms);
    expect(new MzTabReader(members.get("run.pin.mztab") as Buffer).length).toBe(100);
  });
});
