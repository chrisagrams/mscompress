/**
 * Byte-fidelity of decompression against committed archives.
 *
 * The existing msz test only asserts the output is non-empty, so nothing
 * checked that the bytes are actually right. The CLI has always compared them
 * (cli/test/run_test.cmake); the bindings never did.
 *
 * On failure these report where and how the output diverges, because the known
 * failure is Windows-only and reachable only through CI.
 */

import { describe, it, expect, afterEach } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import { read, MZMLFile, MSZFile } from "../src/index.js";
import { MZML_PATH, MSZ_PATH } from "./fixtures.js";

const tmpDirs = new Set<string>();

function makeTmpDir(prefix: string): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpDirs.add(dir);
  return dir;
}

afterEach(() => {
  for (const dir of tmpDirs) {
    try {
      fs.rmSync(dir, { recursive: true, force: true, maxRetries: 10, retryDelay: 50 });
    } catch {
      // best-effort cleanup; the assertions above already covered the happy path
    }
  }
  tmpDirs.clear();
});

/** First divergence plus surrounding context, as an assertion message. */
function describeDifference(expected: Buffer, actual: Buffer): string {
  const sizeNote =
    expected.length !== actual.length
      ? `SIZE DIFFERS: expected ${expected.length}, got ${actual.length}`
      : `sizes match (${expected.length})`;

  const limit = Math.min(expected.length, actual.length);
  let offset = limit;
  for (let i = 0; i < limit; i++) {
    if (expected[i] !== actual[i]) {
      offset = i;
      break;
    }
  }

  if (offset === limit) {
    return `${sizeNote}; common prefix identical, output truncated or extended`;
  }

  const lo = Math.max(0, offset - 32);
  const hi = Math.min(limit, offset + 32);
  return (
    `${sizeNote}; first difference at byte ${offset} ` +
    `(expected 0x${expected[offset].toString(16)}, got 0x${actual[offset].toString(16)})\n` +
    `  expected[${lo}:${hi}] = ${JSON.stringify(expected.subarray(lo, hi).toString("latin1"))}\n` +
    `  actual  [${lo}:${hi}] = ${JSON.stringify(actual.subarray(lo, hi).toString("latin1"))}`
  );
}

/** Decompress `mszPath` into a tracked temp dir and return the output bytes. */
function decompressToBuffer(mszPath: string, prefix: string): Buffer {
  const dir = makeTmpDir(prefix);
  const restored = path.join(dir, "restored.mzML");

  const msz = read(mszPath) as MSZFile;
  let out: MZMLFile | undefined;
  try {
    out = msz.decompress(restored) as MZMLFile;
  } finally {
    // Handles must close before the temp dir is removed, or Windows throws
    // EPERM on unlink.
    out?.close();
    msz.close();
  }

  return fs.readFileSync(restored);
}

describe("decompression byte fidelity", () => {
  it("decompresses a committed .msz to exactly its source mzML", () => {
    // Round-trip tests cannot catch a platform that encodes and decodes with a
    // matching deviation; only committed bytes can.
    const expected = fs.readFileSync(MZML_PATH);
    const actual = decompressToBuffer(MSZ_PATH, "mscompress-fidelity-committed-");

    expect(actual.equals(expected), describeDifference(expected, actual)).toBe(true);
  });

  it("decompresses an archive written by this build to exactly its source", () => {
    // Paired with the test above: if the committed archive fails while this one
    // passes, compression and decompression agree with each other but disagree
    // with the reference bytes.
    const dir = makeTmpDir("mscompress-fidelity-roundtrip-");
    const mszPath = path.join(dir, "roundtrip.msz");

    const file = read(MZML_PATH) as MZMLFile;
    let compressed: MSZFile | undefined;
    try {
      compressed = file.compress(mszPath) as MSZFile;
    } finally {
      compressed?.close();
      file.close();
    }

    const expected = fs.readFileSync(MZML_PATH);
    const actual = decompressToBuffer(mszPath, "mscompress-fidelity-roundtrip-out-");

    expect(actual.equals(expected), describeDifference(expected, actual)).toBe(true);
  });
});
