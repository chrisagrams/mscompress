/**
 * A corrupt division index is rejected at open instead of read out of bounds.
 *
 * Every length stored in the .msz division index is checked against the start
 * of the footer, so a corrupted count makes read() throw instead of crashing.
 */

import { describe, it, expect, afterAll } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { read, MSZFile } from "../src/index.js";
import { MSZ_PATH } from "./fixtures.js";

const FOOTER_SIZE = 88; // sizeof(footer_t)
const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "msz-corrupt-div-"));

afterAll(() => fs.rmSync(tmpDir, { recursive: true, force: true }));

/** Copy test.msz with the first division's spectra count set to `count`. */
function withFirstCount(count: bigint): string {
  const data = Buffer.from(fs.readFileSync(MSZ_PATH));
  const footer = data.length - FOOTER_SIZE;
  const divisionsPos = Number(data.readBigUInt64LE(footer + 6 * 8));
  data.writeBigUInt64LE(count, divisionsPos);
  const out = path.join(tmpDir, `bad-${count}.msz`);
  fs.writeFileSync(out, data);
  return out;
}

describe("Corrupt division index", () => {
  it("still opens a valid file", () => {
    const file = read(MSZ_PATH) as MSZFile;
    expect(file.spectra.length).toBe(50);
    file.close();
  });

  for (const count of [0xffffffffn, 2n ** 61n, 2n ** 64n - 1n]) {
    it(`throws for a spectra count of ${count}`, () => {
      expect(() => read(withFirstCount(count))).toThrow(/failed to read divisions/);
    });
  }
});
