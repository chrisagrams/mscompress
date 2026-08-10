/**
 * Tests for the byte-shuffle transform, exposed as `RuntimeArguments.shuffle`
 * and as a per-call `CompressArgs.shuffle` override.
 *
 * The shuffle is lossless and its state is recorded in the file, so the bar is
 * that a shuffled archive decompresses byte-for-byte back to the source mzML
 * while being measurably smaller than an unshuffled one.
 */

import { describe, it, expect, afterEach } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import { read, MZMLFile, MSZFile } from "../src/index.js";
import { MZML_PATH } from "./fixtures.js";

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
      // best-effort leak cleanup; the tests themselves assert the happy path
    }
  }
  tmpDirs.clear();
});

/**
 * Compress MZML_PATH into `dir` and return the output path. Handles are closed
 * before returning so Windows can unlink the temp dir afterwards.
 */
function compressTo(dir: string, name: string, shuffle: boolean): string {
  const outputPath = path.join(dir, name);
  const file = read(MZML_PATH) as MZMLFile;
  let compressed: MSZFile | undefined;
  try {
    file.arguments.shuffle = shuffle;
    compressed = file.compress(outputPath) as MSZFile;
  } finally {
    compressed?.close();
    file.close();
  }
  return outputPath;
}

describe("byte shuffle", () => {
  it("defaults to off", () => {
    const file = read(MZML_PATH) as MZMLFile;
    try {
      expect(file.arguments.shuffle).toBe(false);
    } finally {
      file.close();
    }
  });

  it("produces a smaller file than the unshuffled baseline", () => {
    const dir = makeTmpDir("mscompress-shuffle-");
    const plain = compressTo(dir, "plain.msz", false);
    const shuffled = compressTo(dir, "shuffled.msz", true);

    const plainBytes = fs.readFileSync(plain);
    const shuffledBytes = fs.readFileSync(shuffled);

    // Guards against the flag silently doing nothing, which a round-trip-only
    // assertion would happily pass.
    expect(shuffledBytes.equals(plainBytes)).toBe(false);
    expect(shuffledBytes.length).toBeLessThan(plainBytes.length);
  });

  it("round trips byte-identically to the source mzML", () => {
    const dir = makeTmpDir("mscompress-shuffle-rt-");
    const shuffled = compressTo(dir, "shuffled.msz", true);
    const restored = path.join(dir, "restored.mzML");

    const msz = read(shuffled) as MSZFile;
    let out: MZMLFile | undefined;
    try {
      out = msz.decompress(restored) as MZMLFile;
    } finally {
      out?.close();
      msz.close();
    }

    expect(fs.readFileSync(restored).equals(fs.readFileSync(MZML_PATH))).toBe(true);
  });

  it("is accepted as a per-call compressStream override", async () => {
    const file = read(MZML_PATH) as MZMLFile;
    try {
      const chunks: Buffer[] = [];
      const stream = file.compressStream({ shuffle: true });
      await new Promise<void>((resolve, reject) => {
        stream.on("data", (c: Buffer) => chunks.push(c));
        stream.on("end", () => resolve());
        stream.on("error", reject);
      });

      const streamed = Buffer.concat(chunks);
      expect(streamed.length).toBeGreaterThan(0);

      // Same args via the file's own settings must give the same bytes.
      const dir = makeTmpDir("mscompress-shuffle-stream-");
      const onDisk = fs.readFileSync(compressTo(dir, "shuffled.msz", true));
      expect(streamed.equals(onDisk)).toBe(true);
    } finally {
      file.close();
    }
  });
});
