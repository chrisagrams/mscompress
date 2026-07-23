import { describe, it, expect } from "vitest";
import { MSZXManifest } from "../src/mszx/mszx-manifest.js";

// This v1-only binding must throw a clear error on a v2/batch manifest instead
// of silently mis-reading it as a single-file archive. See the C CLI's batch
// writer for the v2 `spectra_files` schema it now refuses.
describe("MSZXManifest version guard", () => {
  it("parses a v1 manifest", () => {
    const m = MSZXManifest.parse(
      JSON.stringify({ version: "1.0", spectra_file: "sample.msz", num_spectra: 7 })
    );
    expect(m.version).toBe("1.0");
    expect(m.spectra_file).toBe("sample.msz");
    expect(m.num_spectra).toBe(7);
  });

  it("throws on a v2 batch manifest", () => {
    const json = JSON.stringify({
      version: "2.0",
      container: "batch",
      spectra_files: [{ entry: "a.msz", original: "a.mzML", size: 1 }],
    });
    expect(() => MSZXManifest.parse(json)).toThrow(/newer than this build/);
  });

  it("throws on spectra_files even if mislabeled v1", () => {
    const json = JSON.stringify({
      version: "1.0",
      spectra_files: [{ entry: "a.msz" }],
    });
    expect(() => MSZXManifest.parse(json)).toThrow();
  });
});
