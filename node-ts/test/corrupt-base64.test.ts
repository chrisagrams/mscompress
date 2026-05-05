/**
 * Regression tests for corrupt base64 in mzML files (GitHub issue #111/#118).
 *
 * The test file corrupt_base64.mzML contains 5 spectra where spectrum index 2
 * has an invalid character injected into its m/z base64 data. mscompress should
 * handle this gracefully (empty array) instead of crashing.
 */

import { describe, it, expect, afterEach } from "vitest";
import { read, MZMLFile, Spectrum } from "../src/index.js";
import { CORRUPT_BASE64_MZML_PATH } from "./fixtures.js";

describe("Corrupt base64 mzML (issue #111)", () => {
  let file: MZMLFile | null = null;

  afterEach(() => {
    if (file) {
      file.close();
      file = null;
    }
  });

  it("opens without crashing", () => {
    file = read(CORRUPT_BASE64_MZML_PATH) as MZMLFile;
    expect(file).toBeInstanceOf(MZMLFile);
  });

  it("corrupt spectrum returns empty m/z array", () => {
    file = read(CORRUPT_BASE64_MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(2);
    expect(spectrum.mz.length).toBe(0);
  });

  it("corrupt spectrum still has valid intensity data", () => {
    file = read(CORRUPT_BASE64_MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(2);
    expect(spectrum.intensity.length).toBeGreaterThan(0);
  });

  it("valid spectra are unaffected", () => {
    file = read(CORRUPT_BASE64_MZML_PATH) as MZMLFile;
    for (const i of [0, 1, 3, 4]) {
      const spectrum = file.spectra.get(i);
      expect(spectrum.mz.length).toBeGreaterThan(0);
      expect(spectrum.intensity.length).toBeGreaterThan(0);
    }
  });

  it("dense sequential access does not crash", () => {
    file = read(CORRUPT_BASE64_MZML_PATH) as MZMLFile;
    const spectra = file.spectra;
    expect(spectra.length).toBe(5);
    for (const spectrum of spectra) {
      expect(spectrum).toBeInstanceOf(Spectrum);
      // Access both arrays to trigger decode
      const mz = spectrum.mz;
      const inten = spectrum.intensity;
      expect(mz).toBeDefined();
      expect(inten).toBeDefined();
    }
  });
});
