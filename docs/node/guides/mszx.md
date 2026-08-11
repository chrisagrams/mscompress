# MSZX archives

Bundle MSZ spectra with annotation files into a single tar archive.

## Build

```ts
import { MSZFile, MSZXBuilder } from "mscompress";

const msz = new MSZFile("spectra.msz");

await new MSZXBuilder()
  .setSpectra(msz)
  .setDescription("DDA run #42")
  .setJoinKey("scan_number")
  .addAnnotation({ name: "percolator", file: "results.pin", format: "percolator_pin" })
  .addAnnotation({ name: "comet", file: "search.pep.xml", format: "pepxml" })
  .setExtra({ instrument: "Orbitrap Eclipse" })
  .save("run42.mszx");

msz.close();
```

## Or use `createMSZX()`

```ts
import { createMSZX } from "mscompress";

await createMSZX({
  spectra: "spectra.msz",
  output: "run42.mszx",
  annotations: [
    { name: "percolator", file: "results.pin", format: "percolator_pin" },
  ],
  description: "DDA run #42",
  joinKey: "scan_number",
  extra: { instrument: "Orbitrap Eclipse" },
});
```

## Read

```ts
import { MSZXFile } from "mscompress";

const archive = new MSZXFile("run42.mszx");
console.log(archive.manifest.description);
console.log(archive.manifest.numSpectra);

for (const s of archive.spectra) {
  ...
}

archive.close();
```

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
    writer.addAnnotation(idx, pinBuffer, `annotations/${basename(p)}.pin`, {
      format: "percolator_tsv",
      numRecords: 4021,
    });
    writer.setJoinKey(idx, "scan_number");
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

Annotation payloads are stored as given — compress them yourself with
`zstdCompress` and pass `compressed: true` if you want them compressed.

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
