/**
 * Tests for MZMLFile.compressStream() — no-temp-file MSZ streaming.
 *
 * The gold standard for correctness is that the streamed bytes are byte-for-byte
 * identical to what compress(outputPath) writes to a file for the same args.
 */

import { describe, it, expect } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import { read, MZMLFile, MSZFile } from "../src/index.js";
import { MZML_PATH, SCIEX_MZML_PATH } from "./fixtures.js";

/** Collect a Readable stream into a single Buffer. */
function collect(stream: NodeJS.ReadableStream): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    stream.on("data", (c: Buffer) => chunks.push(c));
    stream.on("end", () => resolve(Buffer.concat(chunks)));
    stream.on("error", reject);
  });
}

/** Compress a fresh MZMLFile to a temp file and return its bytes. */
function compressToBuffer(mzmlPath: string, configure?: (f: MZMLFile) => void): Buffer {
  const file = read(mzmlPath) as MZMLFile;
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-cmp-"));
  const outputPath = path.join(tmpDir, "expected.msz");
  try {
    if (configure) configure(file);
    file.compress(outputPath);
    return fs.readFileSync(outputPath);
  } finally {
    file.close();
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

/** Count `.msz` files sitting directly in the OS temp dir. */
function countTmpMsz(): number {
  return fs.readdirSync(os.tmpdir()).filter((n) => n.endsWith(".msz")).length;
}

describe("MZMLFile.compressStream", () => {
  it("streams bytes byte-for-byte identical to compress()", async () => {
    const expected = compressToBuffer(MZML_PATH);

    const file = read(MZML_PATH) as MZMLFile;
    try {
      const streamed = await collect(file.compressStream());
      expect(streamed.length).toBeGreaterThan(0);
      expect(streamed.equals(expected)).toBe(true);
    } finally {
      file.close();
    }
  });

  it("honors CompressArgs overrides identically to compress() with the same args", async () => {
    // compress() reads args off the file's `arguments`; compressStream() takes
    // them inline. Both paths must produce the same bytes for the same settings.
    const level = 9;
    const expected = compressToBuffer(MZML_PATH, (f) => {
      f.arguments.zstdCompressionLevel = level;
    });

    const file = read(MZML_PATH) as MZMLFile;
    try {
      const streamed = await collect(file.compressStream({ zstdCompressionLevel: level }));
      expect(streamed.equals(expected)).toBe(true);
    } finally {
      file.close();
    }
  });

  it("streamed bytes re-open and round-trip to 50 spectra", async () => {
    const file = read(MZML_PATH) as MZMLFile;
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-rt-"));
    const mszPath = path.join(tmpDir, "streamed.msz");
    try {
      const streamed = await collect(file.compressStream());
      fs.writeFileSync(mszPath, streamed);

      const msz = read(mszPath) as MSZFile;
      expect(msz).toBeInstanceOf(MSZFile);
      expect(msz.spectra.length).toBe(50);

      // Spot-check that a spectrum decodes to real data.
      const spectrum = msz.spectra.get(0);
      expect(spectrum.mz.length).toBeGreaterThan(0);
      expect(spectrum.intensity.length).toBeGreaterThan(0);
      msz.close();
    } finally {
      file.close();
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  });

  it("streams a larger file incrementally (multiple chunks) without buffering", async () => {
    const expected = compressToBuffer(SCIEX_MZML_PATH);

    const file = read(SCIEX_MZML_PATH) as MZMLFile;
    try {
      // A small chunk size forces many reads if data is truly streamed rather
      // than produced as one buffer.
      const stream = file.compressStream({ chunkSize: 4096 });
      const chunks: Buffer[] = [];
      await new Promise<void>((resolve, reject) => {
        stream.on("data", (c: Buffer) => chunks.push(c));
        stream.on("end", resolve);
        stream.on("error", reject);
      });

      expect(chunks.length).toBeGreaterThan(1);
      const streamed = Buffer.concat(chunks);
      expect(streamed.equals(expected)).toBe(true);
    } finally {
      file.close();
    }
  });

  it("leaves no temp .msz files behind (true pipe streaming)", async () => {
    const before = countTmpMsz();
    const file = read(MZML_PATH) as MZMLFile;
    try {
      const streamed = await collect(file.compressStream());
      expect(streamed.length).toBeGreaterThan(0);
    } finally {
      file.close();
    }
    expect(countTmpMsz()).toBe(before);
  });

  it("surfaces failures as a stream 'error' event, not a throw or hang", async () => {
    // Force a native setup failure: a closed handle has no divisions/positions,
    // so streaming cannot start. It must surface as an 'error' event — never a
    // synchronous throw, an unhandled rejection, or a hang.
    const file = read(MZML_PATH) as MZMLFile;
    file.close();

    // Must NOT throw synchronously.
    const stream = file.compressStream();

    const err = await new Promise<Error>((resolve, reject) => {
      stream.on("error", resolve);
      stream.on("data", () => {});
      stream.on("end", () => reject(new Error("stream ended without an error")));
    });
    expect(err).toBeInstanceOf(Error);
  });
});
