import { describe, it, expect } from "vitest";
import {
  MSZXManifest,
  MSZXBatchManifest,
  isBatchManifest,
} from "../src/mszx/mszx-manifest.js";

// v1 (single `spectra_file`) and v2 (multi-file `spectra_files`) are both
// readable. What must still fail loudly is a manifest from a genuinely newer
// mscompress — silently falling back would mis-read an unknown layout.
//
// These assertions were inverted when batch support landed; they previously
// pinned that any v2 manifest threw.
const V2_BATCH = {
  version: "2.0",
  container: "batch",
  spectra_files: [
    { entry: "a.msz", original: "a.mzML", size: 10, num_spectra: 3 },
    { entry: "b.msz", original: "b.mzML", size: 20, num_spectra: 4 },
  ],
};

describe("MSZXManifest version handling", () => {
  it("parses a v1 manifest", () => {
    const m = MSZXManifest.parse(
      JSON.stringify({ version: "1.0", spectra_file: "sample.msz", num_spectra: 7 })
    );
    expect(m.version).toBe("1.0");
    expect(m.spectra_file).toBe("sample.msz");
    expect(m.num_spectra).toBe(7);
  });

  it("routes a v2 batch manifest to the batch parser instead of mis-reading it", () => {
    expect(() => MSZXManifest.parse(JSON.stringify(V2_BATCH))).toThrow(/multi-file/);
  });

  it("treats spectra_files as batch even if mislabeled v1", () => {
    const json = JSON.stringify({ version: "1.0", spectra_files: [{ entry: "a.msz" }] });
    expect(isBatchManifest(JSON.parse(json) as Record<string, unknown>)).toBe(true);
    expect(() => MSZXManifest.parse(json)).toThrow(/multi-file/);
  });
});

describe("MSZXBatchManifest", () => {
  it("parses a v2 batch manifest", () => {
    const m = MSZXBatchManifest.parse(JSON.stringify(V2_BATCH));
    expect(m.container).toBe("batch");
    expect(m.length).toBe(2);
    expect(m.spectra_files.map((e) => e.entry)).toEqual(["a.msz", "b.msz"]);
    expect(m.spectra_files[0].num_spectra).toBe(3);
  });

  it("round-trips through JSON", () => {
    const m = MSZXBatchManifest.parse(JSON.stringify(V2_BATCH));
    expect(MSZXBatchManifest.parse(m.toString()).toJSON()).toEqual(m.toJSON());
  });

  it("tolerates absent optional fields", () => {
    // An archive written before num_spectra/join_key/annotations existed.
    const m = MSZXBatchManifest.parse(
      JSON.stringify({
        version: "2.0",
        container: "batch",
        spectra_files: [{ entry: "a.msz", original: "a.mzML", size: 1 }],
      })
    );
    expect(m.spectra_files[0].num_spectra).toBeUndefined();
    expect(m.spectra_files[0].annotations).toEqual([]);
  });

  it("rejects a single-file manifest", () => {
    expect(() =>
      MSZXBatchManifest.parse(JSON.stringify({ version: "1.0", spectra_file: "a.msz" }))
    ).toThrow(/not a multi-file/);
  });
});

describe("newer major versions are still refused", () => {
  it.each([
    ["MSZXManifest", () => MSZXManifest.parse(JSON.stringify({ version: "3.0" }))],
    [
      "MSZXBatchManifest",
      () =>
        MSZXBatchManifest.parse(
          JSON.stringify({ version: "3.0", container: "batch", spectra_files: [] })
        ),
    ],
  ])("%s", (_name, parse) => {
    expect(parse).toThrow(/newer than this build/);
  });
});
