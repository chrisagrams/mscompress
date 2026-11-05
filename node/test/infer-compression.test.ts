/**
 * Regression tests for mzML files without a compression CV term.
 *
 * When a binaryDataArray has neither MS:1000574 (zlib compression) nor
 * MS:1000576 (no compression), source_compression used to be left
 * uninitialized. The decode function pointers ended up NULL and reading a
 * spectrum segfaulted. The compression is now inferred from the first
 * non-empty spectrum: zlib if its arrays are complete zlib streams, otherwise
 * no compression.
 *
 * Fixtures in test/data/no_compression_term/ are derived from
 * nonindexed_test.mzML (1 spectrum, 1407 peaks, 64-bit m/z and intensity):
 *   - none.mzML: uncompressed data, term removed
 *   - zlib.mzML: zlib-compressed data, term removed
 */

import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { describe, it, expect, afterEach } from "vitest";
import { read, MZMLFile } from "../src/index.js";
import { NONINDEXED_MZML_PATH, NO_COMPRESSION_TERM_DIR } from "./fixtures.js";

const ZLIB = 1000574;
const NO_COMP = 1000576;

describe("mzML without a compression accession", () => {
  const opened: MZMLFile[] = [];
  const open = (p: string): MZMLFile => {
    const f = read(p) as MZMLFile;
    opened.push(f);
    return f;
  };

  afterEach(() => {
    while (opened.length) opened.pop()!.close();
  });

  it.each([
    ["none.mzML", NO_COMP],
    ["zlib.mzML", ZLIB],
  ])("infers source compression for %s", (name, expected) => {
    const file = open(path.join(NO_COMPRESSION_TERM_DIR, name));
    expect(file).toBeInstanceOf(MZMLFile);
    expect(file.format.sourceCompression).toBe(expected);

    // TODO: drop this once Node's FileHandle has a dedicated inflate stream.
    // It currently hands zlib_decompress() a deflate-initialized z_stream, so
    // every zlib-compressed mzML spectrum decodes to an empty array in Node
    // (independent of compression inference).
    if (expected === ZLIB) return;

    const spectrum = file.spectra.get(0);
    const ref = open(NONINDEXED_MZML_PATH).spectra.get(0);
    expect(spectrum.mz.length).toBe(1407);
    expect(Array.from(spectrum.mz)).toEqual(Array.from(ref.mz));
    expect(Array.from(spectrum.intensity)).toEqual(Array.from(ref.intensity));
  });

  it.each(["none.mzML", "zlib.mzML"])("compresses %s", (name) => {
    const file = open(path.join(NO_COMPRESSION_TERM_DIR, name));
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-infer-"));
    try {
      const out = path.join(dir, "out.msz");
      file.compress(out).close();
      expect(fs.statSync(out).size).toBeGreaterThan(0);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
