# MSZX archives

Bundle MSZ spectra with search results into a single tar archive.

Search results are stored **only as mzTab** (1.0, PSM section). Engine output
— pepXML or Percolator TSV/PIN — is converted when the archive is written and
the original file is not stored; mzTab input is stored unchanged. The layout
is shared with the Python binding — see the
[format spec](../../format/mszx.md#search-results-mztab).

## Build

```ts
import { MSZFile, MSZXBuilder } from "mscompress";

const msz = new MSZFile("spectra.msz");

await new MSZXBuilder(msz)
  .addAnnotations("results.pin")                            // -> results.pin.mztab.zst
  .addAnnotations("search.pep.xml", { compressed: false })  // -> search.pep.xml.mztab
  .setDescription("DDA run #42")
  .setExtra("instrument", "Orbitrap Eclipse")
  .save("run42.mszx");

msz.close();
```

`addAnnotations(filePath, { description?, format?, compressed? })` accepts an
mzTab, pepXML or Percolator TSV/PIN file (optionally `.zst`-compressed);
`format` is a hint for the *input* format, used instead of auto-detection. The
member is named `<basename>.mztab` (an mzTab input keeps its name), plus `.zst`
when `compressed` (default `true`, as in the Python builder). The manifest entry's `format` is
`"mztab"`, and the builder's default join key is `"spectra_ref"` (the mzTab
column); change it with `setJoinKey()`.

Two more ways to add search results:

```ts
import { createPSM, createSearchInfo } from "mscompress";

// In-memory PSMs, written as mzTab
builder.addPSMs(
  [createPSM({ scan_number: 1234, peptide: "PEPTIDEK", charge: 2, score: 42.1 })],
  {
    filename: "rescored",                 // -> rescored.mztab.zst (default "psms.mztab")
    searchInfo: createSearchInfo({ software: "MyRescorer", primary_score_name: "score" }),
  }
);

// An mzTab document stored verbatim, under an exact member name
builder.addMzTab(mztabText, { filename: "results.mztab" });
```

Adding two annotations with the same member name throws.

## Or use `createMSZX()`

```ts
import { read, createMSZX, type MSZFile } from "mscompress";

const msz = read("spectra.msz") as MSZFile;
await createMSZX(msz, "run42.mszx", {
  annotations: ["results.pin", "search.pep.xml"],   // converted to mzTab
  description: "DDA run #42",
  extra: { instrument: "Orbitrap Eclipse" },
});
```

## Read

```ts
import { MSZXFile } from "mscompress";

const archive = MSZXFile.open("run42.mszx");
try {
  console.log(archive.manifest.description);
  console.log(archive.manifest.num_spectra);

  for (const s of archive.spectra) {
    ...
  }

  // PSMs of the primary annotation (the first mzTab member)
  for (const psm of archive.psms) {
    console.log(psm.scan_number, psm.peptide, psm.score, psm.q_value);
  }

  // One reader per annotation member, keyed by member name
  for (const [name, reader] of archive.psmReaders) {
    console.log(name, reader.length, reader.searchInfo.software);
  }
  const reader = archive.getPSMReader("results.pin.mztab.zst");
  const best = reader.getBestByScan(1234);
} finally {
  archive.close();
}
```

| Member | Returns |
|--------|---------|
| `psms` | `PSM[]` of the primary annotation (empty if none) |
| `annotationReader` | The primary reader — the first mzTab member, else the first parsable legacy member, else `null` |
| `psmReaders` | `Map<string, PSMCollection>` for every annotation, in manifest order (members that fail to parse are skipped with a warning) |
| `getPSMReader(filename)` | The parsed `PSMCollection` for one member (cached) |
| `getAnnotationFile(filename)` | The member's decompressed bytes |

mzTab members come back as an `MzTabReader`; members of
[older archives](#older-archives) as a `PSMCollection` parsed with the matching
engine parser. Both expose the same query API: `psms`, `length`, `at()`,
`getByScan()`, `getBestByScan()`, `hasScan()`, `scanNumbers`, `searchInfo`,
`sourceFormat`, `sourceName`.

Extracting to a new `.mszx` (`archive.extract("subset.mszx", { scanNumbers:
[...] })`) filters every annotation to the PSMs of the extracted scans and
stores it as compressed mzTab (legacy members are converted on the way).

## Working with search results directly

The annotation helpers are exported for use outside archives:

```ts
import fs from "node:fs";
import { readPSMs, toMzTab, convertToMzTab, MzTabReader, writeMzTab } from "mscompress";

// Parse any supported format (auto-detected; .zst buffers are decompressed)
const collection = readPSMs(fs.readFileSync("search.pep.xml"), "search.pep.xml");
collection.getByScan(1234);

// Convert to mzTab text (mzTab input is returned unchanged)
const text = toMzTab(fs.readFileSync("results.pin"), "results.pin", {
  msRunLocation: "run.msz",
});
// ...or also get the PSM count and detected input format
const { numRecords, inputFormat } = convertToMzTab(fs.readFileSync("results.pin"), "results.pin");

// Read, query and filter an mzTab
const reader = new MzTabReader(fs.readFileSync("results.mztab"));
const subset = reader.filter(new Set([1234, 1240]));    // filtered mzTab text

// Write PSMs you built yourself
const out = writeMzTab(myPsms, { searchInfo, msRunLocation: "run.msz" });
```

`parsePepXML(data, { minRank?, decoyPrefix? })` and
`parsePercolator(data, { decoyPrefix? })` return `{ psms, searchInfo }` for
when you only need one format; `detectAnnotationFormat(text, nameHint?)` and
`mzTabMemberName(name, compressed, isMzTab?)` expose the detection and naming
rules.

## Older archives

Archives written before mzTab became the only stored format carry the
engine's own files (manifest `format` `pepxml`, `percolator_tsv` or `tsv`).
They still open and read through the API above — `getPSMReader` parses each
legacy member with its engine parser. To convert such an archive to mzTab,
use the Python upgrade tool (`python -m mscompress.mszx.upgrade in.mszx [-o
out.mszx]`); see the
[Python MSZX guide](../../python/guides/mszx.md#upgrading-older-archives).

## Batch archives — many runs in one file

A v2 ("batch") archive bundles N independent MSZ files. Use it for a cohort, a
study, or anything you'd otherwise ship as a folder of `.msz`.

### Compress a folder

```ts
import { compressBatch } from "mscompress";

await compressBatch("runs/", "cohort.mszx", { recursive: true, threads: 8 });
```

```ts
await compressBatch(["runA.mzML", "runB.mzML"], "cohort.mszx", {
  description: "cohort A",
  extra: { study_id: "PXD012345" },
  onProgress: (i, total, p) => console.log(`[${i + 1}/${total}] ${p}`),
  mzLossy: "delta32",              // any CompressArgs setting
});
```

### Incremental control, with annotations

```ts
import { MSZXBatchWriter } from "mscompress";

const writer = new MSZXBatchWriter("cohort.mszx", { threads: 8 });
try {
  for (const p of mzmlPaths) {
    const idx = await writer.add(p);
    writer.addPSMs(idx, p.replace(/\.mzML$/i, ".pin"));   // -> <name>.pin.mztab.zst
    writer.setJoinKey(idx, "spectra_ref");
  }
  writer.finish();
} catch (err) {
  writer.abort();                  // no partial archive left behind
  throw err;
}
```

`add()` runs the compression off the event loop and resolves to the entry index.
It accepts a path or an open `MZMLFile`.

Entries are appended sequentially, so **overlapping `add()` calls are rejected** —
`await` each one. Misuse (a finished writer, or an add already in flight) throws
synchronously; an actual compression failure rejects the promise.

`addPSMs(entryIndex, input, options?)` takes a path or a `Buffer` (mzTab,
pepXML or Percolator TSV/PIN), converts it to mzTab, zstd-compresses it unless
`compressed: false`, and returns the member name used. Options:
`filename` (exact member name; default `<input basename>.mztab` plus `.zst`),
`nameHint` (for format detection and naming when `input` is a `Buffer`),
`format` (input format, skips detection), `compressed`, and `msRunLocation`
(the mzTab `ms_run[1]-location`; defaults to the entry's source file name).

The lower-level `addAnnotation()` stores a payload exactly as given — compress
it yourself (zstd) and pass `compressed: true` if you want it
compressed. It does not convert, so pass mzTab with `format: "mztab"` to stay
consistent with the other writers.

### Read a batch archive

```ts
import { read, MSZXBatchFile } from "mscompress";

const archive = read("cohort.mszx") as unknown as MSZXBatchFile;
try {
  console.log(archive.length, archive.names);

  // Spectrum counts come from the manifest — no member is opened
  for (const entry of archive.entries) {
    console.log(entry.entry, entry.num_spectra);
  }

  for (const member of archive) {          // opened lazily, cached
    console.log(member.spectra.length);
  }

  const first = archive.get(0);            // or archive.get("runA.msz")
  archive.decompress("out/");              // every member -> out/*.mzML
} finally {
  archive.close();
}
```

Members are opened on demand and released together by `close()`, so a
1000-member archive doesn't consume 1000 file descriptors unless you touch every
member. Only `manifest.json` is ever read into memory.

### Reading either version uniformly

`MSZXBatchFile` also opens a **v1** archive, as a collection of one — useful when
walking a directory of mixed archives:

```ts
const archive = MSZXBatchFile.open(p);     // v1 -> 1 member, v2 -> N
```

`read()` is unchanged: a v1 archive still returns an `MSZXFile` (which extends
`MSZFile`, so `archive.spectra` works directly). Opting into the collection view
is explicit.

## Reproducibility

Batch archives contain no timestamps, so the same inputs and settings produce
byte-identical output — and the CLI, Python, and Node all drive the same C
writer, so it doesn't matter which one wrote the file.

The output must be a seekable regular file; writing to a pipe or stdout is an
error. See [why](../../format/mszx.md#why-v2-output-must-be-seekable).

See the [MSZX format spec](../../format/mszx.md) for both manifest schemas.
