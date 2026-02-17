import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import { MSZFile, MSZXFile, MSZXBuilder, MSZXManifest, createMSZX } from '../src/index.js';

describe('MSZX', () => {
  let testDir: string;
  let mszPath: string;
  let mszFile: MSZFile;

  beforeAll(() => {
    testDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mszx-test-'));

    // Use an existing MSZ file from test data
    const dataDir = path.join(__dirname, 'data');
    const mszFiles = fs.readdirSync(dataDir).filter((f) => f.endsWith('.msz'));

    if (mszFiles.length === 0) {
      throw new Error('No MSZ test files found');
    }

    mszPath = path.join(dataDir, mszFiles[0]);
    mszFile = new MSZFile(mszPath);
  });

  afterAll(() => {
    if (mszFile) {
      mszFile.close();
    }
    if (testDir && fs.existsSync(testDir)) {
      fs.rmSync(testDir, { recursive: true, force: true });
    }
  });

  describe('MSZXManifest', () => {
    it('should create a manifest with default values', () => {
      const manifest = new MSZXManifest();

      expect(manifest.version).toBe('1.0');
      expect(manifest.spectra_file).toBe('spectra.msz');
      expect(manifest.num_spectra).toBe(0);
      expect(manifest.annotations).toEqual([]);
      expect(manifest.join_key).toBe('scan_number');
      expect(manifest.extra).toEqual({});
    });

    it('should create a manifest with custom values', () => {
      const manifest = new MSZXManifest({
        num_spectra: 100,
        description: 'Test dataset',
        source_file: 'test.mzML',
      });

      expect(manifest.num_spectra).toBe(100);
      expect(manifest.description).toBe('Test dataset');
      expect(manifest.source_file).toBe('test.mzML');
    });

    it('should serialize to JSON and back', () => {
      const original = new MSZXManifest({
        num_spectra: 50,
        description: 'Test',
        annotations: [
          {
            filename: 'test.pin',
            format: 'percolator_tsv',
            compressed: false,
            num_records: 10,
          },
        ],
      });

      const json = original.toString();
      const restored = MSZXManifest.parse(json);

      expect(restored.num_spectra).toBe(original.num_spectra);
      expect(restored.description).toBe(original.description);
      expect(restored.annotations).toEqual(original.annotations);
    });
  });

  describe('MSZXBuilder', () => {
    it('should create a builder from MSZ file', () => {
      const builder = new MSZXBuilder(mszFile);
      expect(builder).toBeDefined();
    });

    it('should set description and metadata', () => {
      const builder = new MSZXBuilder(mszFile);
      builder.setDescription('Test description');
      builder.setJoinKey('custom_key');
      builder.setExtra('key1', 'value1');

      // We can't easily test these without saving, but at least verify no errors
      expect(builder).toBeDefined();
    });

    it('should save an MSZX archive without annotations', async () => {
      const outputPath = path.join(testDir, 'test-simple.mszx');
      const builder = new MSZXBuilder(mszFile);
      builder.setDescription('Simple test archive');

      const result = await builder.save(outputPath);

      expect(result).toBe(outputPath);
      expect(fs.existsSync(outputPath)).toBe(true);
    });

    it('should add .mszx extension if not provided', async () => {
      const outputPath = path.join(testDir, 'test-no-ext');
      const builder = new MSZXBuilder(mszFile);

      const result = await builder.save(outputPath);

      expect(result).toBe(outputPath + '.mszx');
      expect(fs.existsSync(outputPath + '.mszx')).toBe(true);
    });
  });

  describe('MSZXFile', () => {
    let mszxPath: string;
    let mszxFile: MSZXFile;

    beforeAll(async () => {
      // Create a test MSZX file
      mszxPath = path.join(testDir, 'test-for-reading.mszx');
      const builder = new MSZXBuilder(mszFile);
      builder.setDescription('Test archive for reading');
      builder.setExtra('test_key', 'test_value');
      await builder.save(mszxPath);
    });

    afterAll(() => {
      if (mszxFile) {
        mszxFile.close();
      }
    });

    it('should open an MSZX file', () => {
      mszxFile = MSZXFile.open(mszxPath);
      expect(mszxFile).toBeDefined();
      expect(mszxFile.archive_path).toBe(mszxPath);
    });

    it('should read manifest', () => {
      if (!mszxFile) mszxFile = MSZXFile.open(mszxPath);

      const manifest = mszxFile.manifest;
      expect(manifest.description).toBe('Test archive for reading');
      expect(manifest.extra.test_key).toBe('test_value');
      expect(manifest.num_spectra).toBe(mszFile.spectra.length);
    });

    it('should access spectra', () => {
      if (!mszxFile) mszxFile = MSZXFile.open(mszxPath);

      expect(mszxFile.spectra.length).toBe(mszFile.spectra.length);
      expect(mszxFile.spectra.length).toBeGreaterThan(0);

      const firstSpectrum = mszxFile.spectra.get(0);
      expect(firstSpectrum).toBeDefined();
    });

    it('should get binary data', () => {
      if (!mszxFile) mszxFile = MSZXFile.open(mszxPath);

      const mz = mszxFile.getMzBinary(0);
      const inten = mszxFile.getIntenBinary(0);

      expect(mz).toBeDefined();
      expect(inten).toBeDefined();
      expect(mz.length).toBeGreaterThan(0);
      expect(inten.length).toBeGreaterThan(0);
    });

    it('should get XML', () => {
      if (!mszxFile) mszxFile = MSZXFile.open(mszxPath);

      const xml = mszxFile.getXml(0);
      expect(xml).toBeDefined();
      expect(typeof xml).toBe('string');
      expect(xml.length).toBeGreaterThan(0);
    });

    it('should provide describe() with archive info', () => {
      if (!mszxFile) mszxFile = MSZXFile.open(mszxPath);

      const desc = mszxFile.describe();
      expect(desc.archive).toBeDefined();
      expect(desc.archive.path).toBe(mszxPath);
    });

    it('should throw error for non-existent file', () => {
      expect(() => {
        MSZXFile.open(path.join(testDir, 'nonexistent.mszx'));
      }).toThrow('MSZX file not found');
    });
  });

  describe('createMSZX convenience function', () => {
    it('should create an MSZX archive', async () => {
      const outputPath = path.join(testDir, 'test-convenience.mszx');

      const result = await createMSZX(mszFile, outputPath, {
        description: 'Created via convenience function',
      });

      expect(result).toBe(outputPath);
      expect(fs.existsSync(outputPath)).toBe(true);

      // Verify it can be opened
      const mszxFile = MSZXFile.open(outputPath);
      expect(mszxFile.manifest.description).toBe('Created via convenience function');
      mszxFile.close();
    });

    it('should handle extra metadata', async () => {
      const outputPath = path.join(testDir, 'test-extra.mszx');

      await createMSZX(mszFile, outputPath, {
        description: 'Test',
        extra: {
          key1: 'value1',
          key2: 42,
        },
      });

      const mszxFile = MSZXFile.open(outputPath);
      expect(mszxFile.manifest.extra.key1).toBe('value1');
      expect(mszxFile.manifest.extra.key2).toBe(42);
      mszxFile.close();
    });
  });
});
