/**
 * A corrupt division index is rejected at open instead of read out of bounds.
 *
 * Every length stored in the .msz division index is checked against the start
 * of the footer, so a corrupted count makes read() throw instead of crashing.
 */

import { describe, it, expect, afterEach } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import { read, MSZFile } from "../src/index.js";
import { MSZ_PATH } from "./fixtures.js";

const FOOTER_SIZE = 88; // sizeof(footer_t)

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

/** Copy test.msz with the first division's spectra count set to `count`. */
function withFirstCount(count: bigint): string {
  const data = Buffer.from(fs.readFileSync(MSZ_PATH));
  const footer = data.length - FOOTER_SIZE;
  const divisionsPos = Number(data.readBigUInt64LE(footer + 6 * 8));
  data.writeBigUInt64LE(count, divisionsPos);
  const out = path.join(makeTmpDir("msz-corrupt-div-"), "bad.msz");
  fs.writeFileSync(out, data);
  return out;
}

describe("Corrupt division index", () => {
  it("still opens a valid file", () => {
    const file = read(MSZ_PATH) as MSZFile;
    try {
      expect(file.spectra.length).toBe(50);
    } finally {
      file.close();
    }
  });

  for (const count of [0xffffffffn, 2n ** 61n, 2n ** 64n - 1n]) {
    it(`throws for a spectra count of ${count}`, () => {
      expect(() => read(withFirstCount(count))).toThrow(/failed to read divisions/);
    });
  }
});
