/**
 * Member byte offsets in the v2 batch manifest.
 *
 * Each member's `offset` is the absolute position of its payload, so the
 * reader opens it without walking the tar headers. It checks the header at
 * `offset - 512` first and falls back to the walk when it doesn't match.
 */

import { describe, it, expect, beforeAll, afterAll } from "vitest";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as tar from "tar";
import { MSZXBatchFile, MSZXBatchWriter } from "../src/index.js";
import { findManifestJSON } from "../src/mszx/mszx-batch-file.js";
import { MSZX_PATH, TEST_DATA_DIR } from "./fixtures.js";

const TEST_MZML = path.join(TEST_DATA_DIR, "test.mzML");
const ANNOTATION = Buffer.from("scan\tpeptide\n1\tPEPTIDE\n");

let tmpDir: string;
let archivePath: string;

beforeAll(async () => {
  tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-offsets-test-"));
  fs.copyFileSync(TEST_MZML, path.join(tmpDir, "a.mzML"));
  fs.copyFileSync(TEST_MZML, path.join(tmpDir, "b.mzML"));
  archivePath = path.join(tmpDir, "offsets.mszx");

  const writer = new MSZXBatchWriter(archivePath);
  await writer.add(path.join(tmpDir, "a.mzML"));
  const idx = await writer.add(path.join(tmpDir, "b.mzML"));
  writer.addAnnotation(idx, ANNOTATION, "annotations/b.tsv", { format: "tsv" });
  writer.finish();
});

afterAll(() => {
  fs.rmSync(tmpDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
});

/** Member sizes as the tar headers record them. */
function tarSizes(file: string): Map<string, number> {
  const sizes = new Map<string, number>();
  tar.list({
    file,
    sync: true,
    onReadEntry: (entry) => {
      sizes.set(entry.path, entry.size);
      entry.resume();
    },
  });
  return sizes;
}

/** Spectrum counts of every member, opened through the archive. */
function spectrumCounts(file: string): number[] {
  const archive = MSZXBatchFile.open(file);
  try {
    return [...archive].map((m) => m.spectra.length);
  } finally {
    archive.close();
  }
}

/** Copy the archive with `from` replaced by `to` (same length) in the manifest. */
function patchManifest(from: RegExp, to: string, name: string): string {
  const bytes = fs.readFileSync(archivePath);
  const text = bytes.toString("latin1");
  const start = text.lastIndexOf('{\n  "version"');
  const patched = text.slice(start).replace(from, (m) => {
    expect(to.length).toBe(m.length);
    return to;
  });
  const out = path.join(tmpDir, name);
  fs.writeFileSync(out, Buffer.concat([bytes.subarray(0, start), Buffer.from(patched, "latin1")]));
  return out;
}

describe("MSZX v2 member offsets", () => {
  it("records each member's payload offset and size", () => {
    const bytes = fs.readFileSync(archivePath);
    const sizes = tarSizes(archivePath);
    const archive = MSZXBatchFile.open(archivePath);
    try {
      const entries = archive.entries;
      const ann = entries[1].annotations?.[0];
      const members = [...entries, ann].map((m) => ({
        name: m && "entry" in m ? m.entry : (m?.filename ?? ""),
        offset: m?.offset,
        size: m?.size,
      }));
      for (const { name, offset, size } of members) {
        expect(offset).toBeDefined();
        expect(size).toBe(sizes.get(name));
        const header = bytes.subarray((offset as number) - 512, (offset as number) - 512 + name.length);
        expect(header.toString("utf-8")).toBe(name);
      }
      const annOffset = ann?.offset as number;
      expect(bytes.subarray(annOffset, annOffset + ANNOTATION.length)).toEqual(ANNOTATION);
    } finally {
      archive.close();
    }
  });

  it("opens members the same with missing or wrong offsets", () => {
    const expected = spectrumCounts(archivePath);
    expect(expected).toEqual([50, 50]);

    // Same-length edits keep the tar valid: only the manifest payload changes.
    const missing = patchManifest(/"offset"/g, '"offsex"', "missing.mszx");
    const wrong = patchManifest(/"offset": 512,/, '"offset": 513,', "wrong.mszx");
    for (const file of [missing, wrong]) {
      expect(spectrumCounts(file)).toEqual(expected);
    }

    const archive = MSZXBatchFile.open(missing);
    try {
      expect(archive.entries.every((e) => e.offset === undefined)).toBe(true);
    } finally {
      archive.close();
    }
  });
});

describe("findManifestJSON", () => {
  it("finds a v2 manifest by scanning back from the end", () => {
    const json = findManifestJSON(archivePath);
    expect(json).toBeDefined();
    expect(JSON.parse(json as string).container).toBe("batch");
  });

  it("finds a v1 manifest as the first member", () => {
    const json = findManifestJSON(MSZX_PATH);
    expect(json).toBeDefined();
    expect(JSON.parse(json as string).version).toMatch(/^1\./);
  });

  it("returns undefined when the trailer doesn't match", () => {
    const bytes = fs.readFileSync(archivePath);
    const out = path.join(tmpDir, "no-trailer.mszx");
    // Drop the end-of-archive zero blocks: the manifest no longer ends the file.
    fs.writeFileSync(out, bytes.subarray(0, bytes.length - 1024));
    expect(findManifestJSON(out)).toBeUndefined();
  });
});
