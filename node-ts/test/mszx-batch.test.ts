/**
 * Reading v2 multi-file ("batch") MSZX archives.
 *
 * The archives are built by the C CLI when it is available, so these double as
 * cross-language conformance tests: whatever the shared C batch writer emits
 * must be readable by the Node binding.
 */

import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import {
  read,
  MSZXBatchFile,
  MSZXBatchWriter,
  MSZXBatchManifest,
  MSZXFile,
  MZMLFile,
  compressBatch,
  resolveMzmlInputs,
} from "../src/index.js";
import { MSZX_PATH, TEST_DATA_DIR } from "./fixtures.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(__dirname, "../..");
const CLI = path.join(REPO_ROOT, "cli", "mscompress");
const TEST_MZML = path.join(TEST_DATA_DIR, "test.mzML");

const cliAvailable = fs.existsSync(CLI);
const describeCli = cliAvailable ? describe : describe.skip;

let tmpDir: string;
let archivePath: string;

beforeAll(() => {
  if (!cliAvailable) return;
  tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-batch-test-"));
  const src = path.join(tmpDir, "in");
  fs.mkdirSync(src);
  fs.copyFileSync(TEST_MZML, path.join(src, "a.mzML"));
  fs.copyFileSync(TEST_MZML, path.join(src, "b.mzML"));

  archivePath = path.join(tmpDir, "batch.mszx");
  execFileSync(CLI, ["--batch", src, "-o", archivePath], { stdio: "pipe" });
});

afterAll(() => {
  if (tmpDir) {
    fs.rmSync(tmpDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
  }
});

describeCli("MSZXBatchFile", () => {
  it("read() dispatches a batch archive to the batch container", () => {
    const archive = read(archivePath) as unknown as MSZXBatchFile;
    try {
      expect(archive).toBeInstanceOf(MSZXBatchFile);
      expect(archive.length).toBe(2);
      expect(archive.names).toEqual(["a.msz", "b.msz"]);
    } finally {
      archive.close();
    }
  });

  it("exposes a v2 batch manifest", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      expect(archive.manifest.container).toBe("batch");
      expect(archive.manifest.version.startsWith("2.")).toBe(true);
    } finally {
      archive.close();
    }
  });

  it("reads spectrum counts from the manifest without opening a member", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      expect(archive.entries.map((e) => e.num_spectra)).toEqual([50, 50]);
    } finally {
      archive.close();
    }
  });

  it("opens members lazily and caches them", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      const first = archive.get(0);
      expect(first.spectra.length).toBe(50);
      // Same object on re-access — not a second mmap.
      expect(archive.get(0)).toBe(first);
      expect(archive.get("a.msz")).toBe(first);
    } finally {
      archive.close();
    }
  });

  it("agrees between index and name access", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      expect(archive.get(1)).toBe(archive.get("b.msz"));
      expect(archive.has("b.msz")).toBe(true);
      expect(archive.has("nope.msz")).toBe(false);
    } finally {
      archive.close();
    }
  });

  it("iterates every member", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      expect([...archive].map((m) => m.spectra.length)).toEqual([50, 50]);
    } finally {
      archive.close();
    }
  });

  it("throws on an unknown entry", () => {
    const archive = MSZXBatchFile.open(archivePath);
    try {
      expect(() => archive.get("absent.msz")).toThrow(/No such entry/);
      expect(() => archive.get(99)).toThrow(RangeError);
    } finally {
      archive.close();
    }
  });

  it("round-trips every member back to mzML", () => {
    const outDir = path.join(tmpDir, "out");
    const archive = MSZXBatchFile.open(archivePath);
    let written: string[];
    try {
      written = archive.decompress(outDir);
    } finally {
      archive.close();
    }

    expect(written.map((p) => path.basename(p))).toEqual(["a.mzML", "b.mzML"]);
    const original = fs.readFileSync(TEST_MZML);
    for (const p of written) {
      expect(fs.readFileSync(p).equals(original)).toBe(true);
    }
  });

  it("refuses use after close", () => {
    const archive = MSZXBatchFile.open(archivePath);
    archive.close();
    expect(archive.isClosed).toBe(true);
    archive.close(); // idempotent
    expect(() => archive.get(0)).toThrow(/closed/);
  });
});

describe("v1 archives are unaffected", () => {
  it("read() still returns MSZXFile for the committed v1 fixture", () => {
    const file = read(MSZX_PATH);
    try {
      expect(file).toBeInstanceOf(MSZXFile);
      expect(file).not.toBeInstanceOf(MSZXBatchFile);
    } finally {
      file.close();
    }
  });

  it("opens a v1 archive as a one-member collection", () => {
    // One container type reads every .mszx; v1 is a collection of one.
    const archive = MSZXBatchFile.open(MSZX_PATH);
    try {
      expect(archive.length).toBe(1);
      expect(archive.manifest.container).toBe("single");
      expect(archive.manifest.version.startsWith("1.")).toBe(true);
      const entry = archive.entries[0];
      expect(entry.entry).toBe("temp.msz");
      expect(entry.num_spectra).toBe(100);
      // size is absent from a v1 manifest; recovered from the tar header.
      expect(entry.size).toBeGreaterThan(0);
      expect(archive.get(0).spectra.length).toBe(100);
    } finally {
      archive.close();
    }
  });

  it("keeps v1 annotations through the adaptation", () => {
    const archive = MSZXBatchFile.open(MSZX_PATH);
    try {
      expect(archive.entries[0].annotations?.map((a) => a.filename)).toEqual([
        "HSA.pepXML.zst",
      ]);
    } finally {
      archive.close();
    }
  });

  it("has no single .spectra on a collection", () => {
    const archive = MSZXBatchFile.open(MSZX_PATH);
    try {
      expect(() => archive.spectra).toThrow(/archive\.get\(0\)\.spectra/);
    } finally {
      archive.close();
    }
  });

  it("still parses a v1 manifest strictly through the batch manifest parser", () => {
    // The manifest parser stays strict; only the file reader adapts.
    expect(() =>
      MSZXBatchManifest.parse(JSON.stringify({ version: "1.0", spectra_file: "a.msz" }))
    ).toThrow(/not a multi-file/);
  });
});

// --------------------------------------------------------------------------
// Writer
// --------------------------------------------------------------------------

describe("MSZXBatchWriter", () => {
  let writeDir: string;
  let inDir: string;

  beforeAll(() => {
    writeDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-batch-write-"));
    inDir = path.join(writeDir, "in");
    fs.mkdirSync(inDir);
    fs.copyFileSync(TEST_MZML, path.join(inDir, "a.mzML"));
    fs.copyFileSync(TEST_MZML, path.join(inDir, "b.mzML"));
  });

  afterAll(() => {
    fs.rmSync(writeDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
  });

  it("writes a readable archive", async () => {
    const out = path.join(writeDir, "node.mszx");
    await compressBatch(inDir, out);

    const archive = MSZXBatchFile.open(out);
    try {
      expect(archive.names).toEqual(["a.msz", "b.msz"]);
      expect(archive.entries.map((e) => e.num_spectra)).toEqual([50, 50]);
      expect([...archive].map((m) => m.spectra.length)).toEqual([50, 50]);
    } finally {
      archive.close();
    }
  });

  it.runIf(cliAvailable)("produces bytes identical to the CLI", async () => {
    // The whole point of sharing one C writer across producers.
    const cliOut = path.join(writeDir, "cli-cmp.mszx");
    execFileSync(CLI, ["--batch", inDir, "-o", cliOut], { stdio: "pipe" });

    const nodeOut = path.join(writeDir, "node-cmp.mszx");
    await compressBatch(inDir, nodeOut);

    expect(fs.readFileSync(nodeOut).equals(fs.readFileSync(cliOut))).toBe(true);
  });

  it("round-trips back to the original mzML", async () => {
    const out = path.join(writeDir, "rt.mszx");
    await compressBatch(inDir, out);

    const archive = MSZXBatchFile.open(out);
    let written: string[];
    try {
      written = archive.decompress(path.join(writeDir, "rt-out"));
    } finally {
      archive.close();
    }
    const original = fs.readFileSync(TEST_MZML);
    for (const p of written) {
      expect(fs.readFileSync(p).equals(original)).toBe(true);
    }
  });

  it("records description and extra metadata", async () => {
    const out = path.join(writeDir, "meta.mszx");
    await compressBatch(inDir, out, {
      description: "cohort A",
      extra: { study_id: "PXD012345" },
    });

    const archive = MSZXBatchFile.open(out);
    try {
      expect(archive.manifest.description).toBe("cohort A");
      expect(archive.manifest.extra).toEqual({ study_id: "PXD012345" });
    } finally {
      archive.close();
    }
  });

  it("attaches annotations to an entry", async () => {
    const out = path.join(writeDir, "ann.mszx");
    const payload = Buffer.from("scan\tpeptide\n1\tPEPTIDE\n");

    const writer = new MSZXBatchWriter(out);
    const idx = await writer.add(path.join(inDir, "a.mzML"));
    writer.addAnnotation(idx, payload, "annotations/a.tsv", {
      format: "tsv",
      numRecords: 1,
    });
    writer.setJoinKey(idx, "scan_number");
    writer.finish();

    const archive = MSZXBatchFile.open(out);
    try {
      const entry = archive.entries[0];
      expect(entry.join_key).toBe("scan_number");
      expect(entry.annotations?.map((a) => a.filename)).toEqual(["annotations/a.tsv"]);
      expect(entry.annotations?.[0].num_records).toBe(1);
    } finally {
      archive.close();
    }
  });

  it("deduplicates explicit entry names", async () => {
    const out = path.join(writeDir, "names.mszx");
    const writer = new MSZXBatchWriter(out);
    await writer.add(path.join(inDir, "a.mzML"), "same.msz");
    await writer.add(path.join(inDir, "b.mzML"), "same.msz");
    writer.finish();

    const archive = MSZXBatchFile.open(out);
    try {
      expect(archive.names).toEqual(["same.msz", "same__2.msz"]);
    } finally {
      archive.close();
    }
  });

  it("accepts an open MZMLFile", async () => {
    const out = path.join(writeDir, "reuse.mszx");
    const source = new MZMLFile(path.join(inDir, "a.mzML"));
    const writer = new MSZXBatchWriter(out);
    try {
      await writer.add(source);
      writer.finish();
    } finally {
      source.close();
    }

    const archive = MSZXBatchFile.open(out);
    try {
      expect(archive.names).toEqual(["a.msz"]);
    } finally {
      archive.close();
    }
  });

  it("releases the archive so it can be deleted after close", async () => {
    // Windows only permits unlink once the last handle AND mapping are gone,
    // so this asserts close() really releases the per-member mmaps rather than
    // relying on the temp-dir sweep (which is best-effort by design).
    const out = path.join(writeDir, "deletable.mszx");
    await compressBatch(inDir, out);

    const archive = MSZXBatchFile.open(out);
    expect(archive.get(0).spectra.length).toBe(50); // force a member mmap
    archive.close();

    fs.unlinkSync(out);
    expect(fs.existsSync(out)).toBe(false);
  });

  it("leaves no archive behind on abort", async () => {
    const out = path.join(writeDir, "aborted.mszx");
    const writer = new MSZXBatchWriter(out);
    await writer.add(path.join(inDir, "a.mzML"));
    writer.abort();
    expect(fs.existsSync(out)).toBe(false);
  });

  it("rejects a non-mzML input", async () => {
    const junk = path.join(writeDir, "junk.txt");
    fs.writeFileSync(junk, "not an mzML");
    const writer = new MSZXBatchWriter(path.join(writeDir, "bad.mszx"));
    try {
      await expect(writer.add(junk)).rejects.toThrow(/could not add/);
    } finally {
      writer.abort();
    }
  });

  it("refuses overlapping adds", async () => {
    // Entries append sequentially; interleaving them would corrupt the archive.
    const out = path.join(writeDir, "concurrent.mszx");
    const writer = new MSZXBatchWriter(out);
    try {
      const first = writer.add(path.join(inDir, "a.mzML"));
      expect(() => writer.add(path.join(inDir, "b.mzML"))).toThrow(
        /already in flight/
      );
      await first;
    } finally {
      writer.abort();
    }
  });

  it("refuses use after finish", async () => {
    const out = path.join(writeDir, "done.mszx");
    const writer = new MSZXBatchWriter(out);
    await writer.add(path.join(inDir, "a.mzML"));
    writer.finish();
    // Synchronous throw: the writer handle is gone, so there is nothing to await.
    expect(() => writer.add(path.join(inDir, "b.mzML"))).toThrow(/finished or aborted/);
  });

  it("throws when nothing matches", async () => {
    const empty = path.join(writeDir, "empty");
    fs.mkdirSync(empty, { recursive: true });
    await expect(compressBatch(empty, path.join(writeDir, "none.mszx"))).rejects.toThrow(
      /No input mzML files matched/
    );
  });

  it("reports progress per entry", async () => {
    const seen: Array<[number, number, string]> = [];
    await compressBatch(inDir, path.join(writeDir, "prog.mszx"), {
      onProgress: (i, total, p) => seen.push([i, total, path.basename(p)]),
    });
    expect(seen).toEqual([
      [0, 2, "a.mzML"],
      [1, 2, "b.mzML"],
    ]);
  });

  it("resolves inputs deterministically and deduplicated", () => {
    const a = path.join(inDir, "a.mzML");
    const resolved = resolveMzmlInputs([inDir, a, a]);
    expect(resolved.map((p) => path.basename(p))).toEqual(["a.mzML", "b.mzML"]);
  });
});
