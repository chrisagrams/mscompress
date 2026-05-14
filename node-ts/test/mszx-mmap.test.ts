/**
 * Tests for the first-class MSZXFile reader.
 *
 * MSZXFile extends MSZFile and mmaps the embedded MSZ payload directly
 * out of the .mszx archive at the entry's byte offset. These tests verify:
 *   - no temp directory is created on open;
 *   - data read through the mmap'd MSZ is byte-identical to data read from
 *     the same MSZ extracted to a standalone file;
 *   - directory-output decompress writes mzML + annotation files together;
 *   - MSZXFile is polymorphic with MSZFile (instanceof).
 */

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import * as tar from "tar";
import { afterAll, beforeAll, describe, expect, it } from "vitest";
import { MSZFile } from "@/files/msz-file.js";
import { MZMLFile } from "@/files/mzml-file.js"; // side-effect: register "mzml" factory
import { MSZXFile } from "@/mszx/mszx-file.js";
import { MSZX_PATH } from "./fixtures.js";

// Reference MZMLFile so the side-effect import isn't dropped.
void MZMLFile;

describe("MSZXFile (mmap-into-tar)", () => {
  it("does not create a mszx-XXXXXX temp dir on open", () => {
    // The old impl used mkdtempSync(prefix="mszx-") which produces names
    // like "mszx-aB3xYz". Other test fixtures use longer prefixes
    // ("mszx-test-", "mszx-build-", "mszx-extract-"), so we match the
    // bare 6-suffix shape to specifically detect the old behavior.
    const oldPattern = /^mszx-[A-Za-z0-9]{6}$/;
    const tmp = os.tmpdir();
    const before = new Set(fs.readdirSync(tmp).filter((e) => oldPattern.test(e)));
    const mszx = MSZXFile.open(MSZX_PATH);
    try {
      const after = new Set(fs.readdirSync(tmp).filter((e) => oldPattern.test(e)));
      const newEntries = [...after].filter((e) => !before.has(e));
      expect(newEntries).toEqual([]);
    } finally {
      mszx.close();
    }
  });

  it("is polymorphic with MSZFile (instanceof)", () => {
    const mszx = MSZXFile.open(MSZX_PATH);
    try {
      expect(mszx).toBeInstanceOf(MSZFile);
    } finally {
      mszx.close();
    }
  });

  describe("byte-equivalence vs standalone-extracted MSZ", () => {
    let standalonePath: string;
    let standaloneTmpDir: string;

    beforeAll(() => {
      // Manually extract the embedded MSZ into a temp dir so we can open
      // it as a standalone MSZFile and compare byte-for-byte.
      standaloneTmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-verify-"));
      tar.extract({ file: MSZX_PATH, cwd: standaloneTmpDir, sync: true });
      const entries = fs.readdirSync(standaloneTmpDir).filter((f) => f.endsWith(".msz"));
      if (entries.length !== 1) {
        throw new Error(`expected exactly one .msz entry, found ${entries.length}`);
      }
      standalonePath = path.join(standaloneTmpDir, entries[0]);
    });

    afterAll(() => {
      fs.rmSync(standaloneTmpDir, { recursive: true, force: true });
    });

    it("yields identical mz/intensity arrays for sampled spectra", () => {
      const direct = new MSZFile(standalonePath);
      const viaArchive = MSZXFile.open(MSZX_PATH);
      try {
        expect(viaArchive.spectra.length).toBe(direct.spectra.length);

        const sample = [
          0,
          Math.floor(direct.spectra.length / 2),
          direct.spectra.length - 1,
        ];
        for (const i of sample) {
          const dMz = direct.getMzBinary(i);
          const aMz = viaArchive.getMzBinary(i);
          expect(Buffer.from(aMz.buffer, aMz.byteOffset, aMz.byteLength).equals(
            Buffer.from(dMz.buffer, dMz.byteOffset, dMz.byteLength)
          )).toBe(true);

          const dInt = direct.getIntenBinary(i);
          const aInt = viaArchive.getIntenBinary(i);
          expect(Buffer.from(aInt.buffer, aInt.byteOffset, aInt.byteLength).equals(
            Buffer.from(dInt.buffer, dInt.byteOffset, dInt.byteLength)
          )).toBe(true);
        }
      } finally {
        direct.close();
        viaArchive.close();
      }
    });
  });

  it("decompress(<dir>) writes mzML + annotation files into the directory", () => {
    const outDir = fs.mkdtempSync(path.join(os.tmpdir(), "mszx-dir-out-"));
    try {
      const mszx = MSZXFile.open(MSZX_PATH);
      try {
        // `decompress` returns an open MZMLFile wrapping the freshly-written
        // mzML. It must be closed before rmSync, else Windows fails with
        // EPERM (the mmap keeps the file locked).
        const produced = mszx.decompress(outDir);
        produced.close();
      } finally {
        mszx.close();
      }

      const stem = path.basename(MSZX_PATH, path.extname(MSZX_PATH));
      const mzmlPath = path.join(outDir, `${stem}.mzML`);
      expect(fs.existsSync(mzmlPath)).toBe(true);
      expect(fs.statSync(mzmlPath).size).toBeGreaterThan(0);

      const files = fs.readdirSync(outDir);
      const annotationFiles = files.filter((f) => f !== `${stem}.mzML`);
      expect(annotationFiles.length).toBeGreaterThan(0);
      // .zst suffixes must be stripped on the way out.
      expect(annotationFiles.some((f) => f.endsWith(".zst"))).toBe(false);
    } finally {
      fs.rmSync(outDir, { recursive: true, force: true });
    }
  });
});
