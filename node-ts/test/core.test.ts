import { describe, it, expect } from "vitest";
import os from "node:os";
import fs from "node:fs";
import path from "node:path";
import { getNumThreads, getFilesize, getVersion, read } from "../src/index.js";

const MZML_PATH = path.resolve(__dirname, "data/test.mzML");

describe("core", () => {
  it("getNumThreads returns os.cpus().length", () => {
    expect(getNumThreads()).toBe(os.cpus().length);
  });

  it("getVersion returns a version string", () => {
    const version = getVersion();
    expect(typeof version).toBe("string");
    expect(version).toMatch(/^\d+\.\d+\.\d+$/);
  });

  it("getFilesize returns correct size", () => {
    const expected = fs.statSync(MZML_PATH).size;
    expect(getFilesize(MZML_PATH)).toBe(expected);
  });

  it("getFilesize returns 0 for invalid path", () => {
    // C get_filesize returns 0 for invalid paths
    expect(getFilesize("ABC123_nonexistent")).toBe(0);
  });

  it("read throws on nonexistent file", () => {
    expect(() => read("ABC123_nonexistent")).toThrow();
  });

  it("read throws on directory", () => {
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "mscompress-test-"));
    try {
      expect(() => read(tmpDir)).toThrow(/directory/i);
    } finally {
      fs.rmdirSync(tmpDir);
    }
  });

  it("read throws on invalid parameter type", () => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    expect(() => read({} as any)).toThrow();
  });
});
