import { describe, it, expect, afterEach } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import {
  read,
  MZMLFile,
  MSZFile,
  DataFormat,
  Division,
  Spectra,
  Spectrum,
  DataPositions,
} from "../src/index.js";
import { MSZ_PATH } from "./fixtures.js";

describe("MSZFile", () => {
  let file: MSZFile | null = null;

  afterEach(() => {
    if (file) {
      file.close();
      file = null;
    }
  });

  it("read() returns MSZFile for .msz", () => {
    file = read(MSZ_PATH) as MSZFile;
    expect(file).toBeInstanceOf(MSZFile);
    expect(file.path).toBe(MSZ_PATH);
    expect(file.filesize).toBe(fs.statSync(MSZ_PATH).size);
  });

  it("close() releases resources", () => {
    file = read(MSZ_PATH) as MSZFile;
    file.close();
    file.close(); // double close should not throw
    file = null;
  });

  it("describe() returns expected shape", () => {
    file = read(MSZ_PATH) as MSZFile;
    const desc = file.describe();
    expect(desc).toHaveProperty("path");
    expect(desc).toHaveProperty("filesize");
    expect(desc).toHaveProperty("format");
    expect(desc).toHaveProperty("positions");
    expect(typeof desc.path).toBe("string");
    expect(typeof desc.filesize).toBe("number");
    expect(desc.format).toBeInstanceOf(DataFormat);
    expect(desc.positions).toBeInstanceOf(Division);
  });

  it("spectra returns iterable Spectra with correct length", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectra = file.spectra;
    expect(spectra).toBeInstanceOf(Spectra);
    expect(typeof spectra.length).toBe("number");
    expect(spectra.length).toBeGreaterThan(0);

    let count = 0;
    for (const spectrum of spectra) {
      expect(spectrum).toBeInstanceOf(Spectrum);
      count++;
    }
    expect(count).toBe(spectra.length);
  });

  it("out-of-bounds index throws", () => {
    file = read(MSZ_PATH) as MSZFile;
    expect(() => file!.spectra.get(file!.spectra.length + 1)).toThrow();
  });

  it("Spectrum.mz returns TypedArray", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(0);
    const mz = spectrum.mz;
    expect(mz instanceof Float64Array || mz instanceof Float32Array).toBe(true);
    expect(mz.length).toBeGreaterThan(0);
  });

  it("Spectrum.intensity returns TypedArray", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(0);
    const inten = spectrum.intensity;
    expect(inten instanceof Float64Array || inten instanceof Float32Array).toBe(true);
    expect(inten.length).toBeGreaterThan(0);
  });

  it("Spectrum.peaks returns array", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(1);
    const peaks = spectrum.peaks;
    expect(peaks).toBeInstanceOf(Float64Array);
    expect(peaks.length).toBeGreaterThan(0);
  });

  it("Spectrum.xml returns string", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(0);
    const xml = spectrum.xml;
    expect(typeof xml).toBe("string");
    expect(xml.length).toBeGreaterThan(0);
  });

  it("Spectrum.msLevel is positive", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(0);
    expect(typeof spectrum.msLevel).toBe("number");
    expect(spectrum.msLevel).toBeGreaterThan(0);
  });

  it("Spectrum.size is positive", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectrum = file.spectra.get(0);
    expect(spectrum.size).toBeGreaterThan(0);
  });

  it("positions returns Division with sub-DataPositions", () => {
    file = read(MSZ_PATH) as MSZFile;
    const pos = file.positions;
    expect(pos).toBeInstanceOf(Division);
    expect(typeof pos.size).toBe("number");
    expect(pos.spectra).toBeInstanceOf(DataPositions);
    expect(pos.xml).toBeInstanceOf(DataPositions);
    expect(pos.mz).toBeInstanceOf(DataPositions);
    expect(pos.inten).toBeInstanceOf(DataPositions);
  });

  it("DataPositions arrays match totalSpec length", () => {
    file = read(MSZ_PATH) as MSZFile;
    const dp = file.positions.spectra;
    expect(dp.startPositions).toBeInstanceOf(Float64Array);
    expect(dp.endPositions).toBeInstanceOf(Float64Array);
    expect(dp.startPositions.length).toBe(dp.totalSpec);
    expect(dp.endPositions.length).toBe(dp.totalSpec);
  });

  it("format returns DataFormat with valid fields", () => {
    file = read(MSZ_PATH) as MSZFile;
    const fmt = file.format;
    expect(fmt).toBeInstanceOf(DataFormat);
    expect(typeof fmt.sourceMzFmt).toBe("number");
    expect(typeof fmt.sourceIntenFmt).toBe("number");
    expect(typeof fmt.sourceCompression).toBe("number");
  });

  it("format.toDict() returns MS: accession strings", () => {
    file = read(MSZ_PATH) as MSZFile;
    const dict = file.format.toDict();
    expect(String(dict.sourceMzFmt)).toMatch(/^MS:\d+$/);
    expect(String(dict.sourceIntenFmt)).toMatch(/^MS:\d+$/);
    expect(String(dict.sourceCompression)).toMatch(/^MS:\d+$/);
  });

  it("decompress MSZ to mzML", () => {
    file = read(MSZ_PATH) as MSZFile;
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-test-"));
    const outputPath = path.join(tmpDir, "test_output.mzML");

    try {
      const mzml = file.decompress(outputPath);
      expect(fs.existsSync(outputPath)).toBe(true);
      expect(fs.statSync(outputPath).size).toBeGreaterThan(0);
      expect(mzml).toBeInstanceOf(MZMLFile);
      mzml.close();
    } finally {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  it("compress() throws on MSZFile", () => {
    file = read(MSZ_PATH) as MSZFile;
    expect(() => file!.compress("test.out")).toThrow();
  });

  it("retention time values match known values", () => {
    file = read(MSZ_PATH) as MSZFile;
    const spectra = file.spectra;

    const rt0 = spectra.get(0).retentionTime!;
    // MSZ stores retention times as float32, so use wider tolerance
    expect(Math.abs(rt0 - 0.21442476)).toBeLessThan(1e-2);

    const rt10 = spectra.get(10).retentionTime!;
    expect(Math.abs(rt10 - 1.15352136)).toBeLessThan(1e-2);
  });
});
