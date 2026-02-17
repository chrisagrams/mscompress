import { describe, it, expect } from "vitest";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { read, MZMLFile, MSZFile } from "../src/index.js";
import { MZML_PATH, MSZ_PATH } from "./fixtures.js";

function withTmpDir(fn: (tmpDir: string) => void) {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-test-"));
  try {
    fn(tmpDir);
  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

describe("MSZ -> mzML extraction", () => {
  it("extract by indices", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { indices: [0, 2, 4] });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        expect(extracted.spectra.length).toBe(3);
        expect(extracted.spectra.get(0).scan).toBe(1);
        expect(extracted.spectra.get(1).scan).toBe(3);
        expect(extracted.spectra.get(2).scan).toBe(5);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by scan numbers", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { scanNumbers: [2, 4] });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(2);
        expect(extracted.spectra.get(1).scan).toBe(4);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by ms_level", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { msLevel: 2 });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        for (const spectrum of extracted.spectra) {
          expect(spectrum.msLevel).toBe(2);
        }
      } finally {
        extracted.close();
      }
    });
  });
});

describe("MSZ -> MSZ extraction", () => {
  it("extract by indices", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { indices: [1, 3] });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(2);
        expect(extracted.spectra.get(1).scan).toBe(4);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by scan numbers", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { scanNumbers: [1, 5] });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(1);
        expect(extracted.spectra.get(1).scan).toBe(5);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by ms_level", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const msz = read(MSZ_PATH);
      try {
        msz.extract(outputPath, { msLevel: 1 });
      } finally {
        msz.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        for (const spectrum of extracted.spectra) {
          expect(spectrum.msLevel).toBe(1);
        }
      } finally {
        extracted.close();
      }
    });
  });
});

describe("mzML -> mzML extraction", () => {
  it("extract by indices", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { indices: [0, 2, 4] });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        expect(extracted.spectra.length).toBe(3);
        expect(extracted.spectra.get(0).scan).toBe(1);
        expect(extracted.spectra.get(1).scan).toBe(3);
        expect(extracted.spectra.get(2).scan).toBe(5);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by scan numbers", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { scanNumbers: [2, 4] });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(2);
        expect(extracted.spectra.get(1).scan).toBe(4);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by ms_level", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.mzML");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { msLevel: 2 });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MZMLFile;
      try {
        for (const spectrum of extracted.spectra) {
          expect(spectrum.msLevel).toBe(2);
        }
      } finally {
        extracted.close();
      }
    });
  });
});

describe("mzML -> MSZ extraction", () => {
  it("extract by indices", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { indices: [1, 3] });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(2);
        expect(extracted.spectra.get(1).scan).toBe(4);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by scan numbers", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { scanNumbers: [1, 5] });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        expect(extracted.spectra.length).toBe(2);
        expect(extracted.spectra.get(0).scan).toBe(1);
        expect(extracted.spectra.get(1).scan).toBe(5);
      } finally {
        extracted.close();
      }
    });
  });

  it("extract by ms_level", () => {
    withTmpDir((tmpDir) => {
      const outputPath = path.join(tmpDir, "extracted.msz");
      const mzml = read(MZML_PATH);
      try {
        mzml.extract(outputPath, { msLevel: 1 });
      } finally {
        mzml.close();
      }

      const extracted = read(outputPath) as MSZFile;
      try {
        for (const spectrum of extracted.spectra) {
          expect(spectrum.msLevel).toBe(1);
        }
      } finally {
        extracted.close();
      }
    });
  });
});
