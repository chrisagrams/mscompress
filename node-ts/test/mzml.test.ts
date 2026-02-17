import { describe, it, expect, afterEach } from 'vitest';
import os from 'node:os';
import fs from 'node:fs';
import path from 'node:path';
import {
  read, MZMLFile, MSZFile, DataFormat, Division, Spectra, Spectrum,
  DataPositions, RuntimeArguments,
} from '../src/index.js';

const MZML_PATH = path.resolve(__dirname, 'data/test.mzML');

describe('MZMLFile', () => {
  let file: MZMLFile | null = null;

  afterEach(() => {
    if (file) {
      file.close();
      file = null;
    }
  });

  it('read() returns MZMLFile for .mzML', () => {
    file = read(MZML_PATH) as MZMLFile;
    expect(file).toBeInstanceOf(MZMLFile);
    expect(file.path).toBe(MZML_PATH);
    expect(file.filesize).toBe(fs.statSync(MZML_PATH).size);
  });

  it('close() releases resources', () => {
    file = read(MZML_PATH) as MZMLFile;
    file.close();
    // Should not throw on double close
    file.close();
    file = null;
  });

  it('describe() returns expected shape', () => {
    file = read(MZML_PATH) as MZMLFile;
    const desc = file.describe();
    expect(desc).toHaveProperty('path');
    expect(desc).toHaveProperty('filesize');
    expect(desc).toHaveProperty('format');
    expect(desc).toHaveProperty('positions');
    expect(typeof desc.path).toBe('string');
    expect(typeof desc.filesize).toBe('number');
    expect(desc.format).toBeInstanceOf(DataFormat);
    expect(desc.positions).toBeInstanceOf(Division);
  });

  it('spectra returns iterable Spectra with correct length', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectra = file.spectra;
    expect(spectra).toBeInstanceOf(Spectra);
    expect(typeof spectra.length).toBe('number');
    expect(spectra.length).toBeGreaterThan(0);

    let count = 0;
    for (const spectrum of spectra) {
      expect(spectrum).toBeInstanceOf(Spectrum);
      count++;
    }
    expect(count).toBe(spectra.length);
  });

  it('out-of-bounds index throws', () => {
    file = read(MZML_PATH) as MZMLFile;
    expect(() => file!.spectra.get(file!.spectra.length + 1)).toThrow();
  });

  it('Spectrum.mz returns TypedArray with correct dtype', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    const mz = spectrum.mz;
    expect(mz).toBeInstanceOf(Float64Array);
    expect(mz.length).toBeGreaterThan(0);
  });

  it('Spectrum.intensity returns TypedArray', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    const inten = spectrum.intensity;
    expect(inten instanceof Float64Array || inten instanceof Float32Array).toBe(true);
    expect(inten.length).toBeGreaterThan(0);
  });

  it('Spectrum.peaks returns interleaved array', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    const peaks = spectrum.peaks;
    expect(peaks).toBeInstanceOf(Float64Array);
    expect(peaks.length).toBeGreaterThan(0);
    expect(peaks.length).toBe(spectrum.mz.length * 2);
  });

  it('Spectrum.msLevel is positive int', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    expect(typeof spectrum.msLevel).toBe('number');
    expect(spectrum.msLevel).toBeGreaterThan(0);
  });

  it('Spectrum.retentionTime is number', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    expect(spectrum.retentionTime).not.toBeNull();
    expect(typeof spectrum.retentionTime).toBe('number');
  });

  it('Spectrum.size is positive int', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectrum = file.spectra.get(0);
    expect(typeof spectrum.size).toBe('number');
    expect(spectrum.size).toBeGreaterThan(0);
  });

  it('positions returns Division with sub-DataPositions', () => {
    file = read(MZML_PATH) as MZMLFile;
    const pos = file.positions;
    expect(pos).toBeInstanceOf(Division);
    expect(typeof pos.size).toBe('number');
    expect(pos.spectra).toBeInstanceOf(DataPositions);
    expect(pos.xml).toBeInstanceOf(DataPositions);
    expect(pos.mz).toBeInstanceOf(DataPositions);
    expect(pos.inten).toBeInstanceOf(DataPositions);
  });

  it('DataPositions arrays match totalSpec length', () => {
    file = read(MZML_PATH) as MZMLFile;
    const dp = file.positions.spectra;
    expect(dp.startPositions).toBeInstanceOf(Float64Array);
    expect(dp.endPositions).toBeInstanceOf(Float64Array);
    expect(typeof dp.totalSpec).toBe('number');
    expect(dp.startPositions.length).toBe(dp.totalSpec);
    expect(dp.endPositions.length).toBe(dp.totalSpec);
  });

  it('format returns DataFormat with valid fields', () => {
    file = read(MZML_PATH) as MZMLFile;
    const fmt = file.format;
    expect(fmt).toBeInstanceOf(DataFormat);
    expect(typeof fmt.sourceMzFmt).toBe('number');
    expect(typeof fmt.sourceIntenFmt).toBe('number');
    expect(typeof fmt.sourceCompression).toBe('number');
  });

  it('format.toDict() returns MS: accession strings', () => {
    file = read(MZML_PATH) as MZMLFile;
    const dict = file.format.toDict();
    expect(String(dict.sourceMzFmt)).toMatch(/^MS:\d+$/);
    expect(String(dict.sourceIntenFmt)).toMatch(/^MS:\d+$/);
    expect(String(dict.sourceCompression)).toMatch(/^MS:\d+$/);
  });

  it('arguments returns RuntimeArguments', () => {
    file = read(MZML_PATH) as MZMLFile;
    expect(file.arguments).toBeInstanceOf(RuntimeArguments);
  });

  it('arguments.threads can be set and read', () => {
    file = read(MZML_PATH) as MZMLFile;
    file.arguments.threads = 1;
    expect(file.arguments.threads).toBe(1);
  });

  it('arguments.zstdCompressionLevel can be set and read', () => {
    file = read(MZML_PATH) as MZMLFile;
    file.arguments.zstdCompressionLevel = 1;
    expect(file.arguments.zstdCompressionLevel).toBe(1);
  });

  it('compress mzML to MSZ', () => {
    file = read(MZML_PATH) as MZMLFile;
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mscompress-test-'));
    const outputPath = path.join(tmpDir, 'test_output.msz');

    try {
      const msz = file.compress(outputPath);
      expect(fs.existsSync(outputPath)).toBe(true);
      expect(fs.statSync(outputPath).size).toBeGreaterThan(0);
      expect(msz).toBeInstanceOf(MSZFile);
      msz.close();
    } finally {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  it('compress with threads > spectra count succeeds', () => {
    file = read(MZML_PATH) as MZMLFile;
    file.arguments.threads = 100;
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mscompress-test-'));
    const outputPath = path.join(tmpDir, 'test_output_threads.msz');

    try {
      const msz = file.compress(outputPath);
      expect(fs.existsSync(outputPath)).toBe(true);
      expect(fs.statSync(outputPath).size).toBeGreaterThan(0);
      expect(msz).toBeInstanceOf(MSZFile);
      msz.close();
    } finally {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  it('decompress() throws on MZMLFile', () => {
    file = read(MZML_PATH) as MZMLFile;
    expect(() => file!.decompress('test.out')).toThrow();
  });

  it('retention time values match known values', () => {
    file = read(MZML_PATH) as MZMLFile;
    const spectra = file.spectra;

    const rt0 = spectra.get(0).retentionTime!;
    expect(Math.abs(rt0 - 0.21442476)).toBeLessThan(1e-4);

    const rt10 = spectra.get(10).retentionTime!;
    expect(Math.abs(rt10 - 1.15352136)).toBeLessThan(1e-4);
  });
});
