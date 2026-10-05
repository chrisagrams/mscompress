# MSZX archives

Bundle an MSZ-compressed spectra file with search results into a single
self-describing archive.

## Build

```python
from mscompress import MSZFile, MSZXBuilder

with MSZFile("spectra.msz") as msz:
    (
        MSZXBuilder()
            .set_spectra(msz)
            .set_description("DDA run #42")
            .set_join_key("scan_number")
            .add_annotation("percolator", "results.pin", format="percolator_pin")
            .add_annotation("comet", "search.pep.xml", format="pepxml")
            .set_extra(instrument="Orbitrap Eclipse")
            .save("run42.mszx")
    )
```

## Or use the convenience function

```python
from mscompress import create_mszx

create_mszx(
    spectra="spectra.msz",
    output="run42.mszx",
    annotations=[
        ("percolator", "results.pin", "percolator_pin"),
        ("comet", "search.pep.xml", "pepxml"),
    ],
    description="DDA run #42",
    join_key="scan_number",
    extra={"instrument": "Orbitrap Eclipse"},
)
```

## Read

```python
from mscompress import MSZXFile

with MSZXFile("run42.mszx") as archive:
    print(archive.manifest.description)
    print(archive.manifest.num_spectra)

    # Iterate spectra
    for s in archive.spectra:
        ...

    # Iterate annotations
    for entry in archive.manifest.annotations:
        reader = archive.open_annotation(entry.name)
        for psm in reader:
            print(psm.scan_number, psm.peptide, psm.score)
```

## Batch archives — many runs in one file

A v2 ("batch") archive bundles N independent MSZ files. Use it for a cohort, a
study, or anything you'd otherwise ship as a folder of `.msz`.

### Compress a folder

```python
from mscompress import compress_batch

compress_batch("runs/", "cohort.mszx", recursive=True, threads=8)
```

`inputs` accepts a path, a directory, a glob, or an iterable of any of those.
Directories contribute `*.mzML`; explicitly named files are taken as-is.

```python
compress_batch(
    ["runA.mzML", "runB.mzML"],
    "cohort.mszx",
    description="cohort A",
    extra={"study_id": "PXD012345"},
    on_progress=lambda i, total, path: print(f"[{i + 1}/{total}] {path.name}"),
    mz_lossy="delta32",          # any RuntimeArguments setting
)
```

### Incremental control, with annotations

```python
from mscompress import MSZXBatchWriter

with MSZXBatchWriter("cohort.mszx", threads=8) as writer:
    for path in mzml_paths:
        idx = writer.add(path)                      # -> entry index
        writer.add_annotation(
            idx, pin_bytes, f"annotations/{path.stem}.pin",
            format="percolator_tsv", num_records=4021,
        )
        writer.set_join_key(idx, "scan_number")
```

Leaving the `with` block via an exception calls `abort()`, so a partial archive
is never left behind. `add()` also accepts an already-open `MZMLFile`, reusing
its mapping rather than re-opening the file.

Annotation payloads are stored as given — compress them yourself and pass
`compressed=True` if you want them zstd-compressed.

### Read a batch archive

```python
from mscompress import read

with read("cohort.mszx") as archive:          # -> MSZXBatchFile
    print(len(archive), archive.names)

    # Spectrum counts come from the manifest — no member is opened
    for entry in archive.entries:
        print(entry.entry, entry.num_spectra)

    for member in archive:                    # opened lazily, cached
        print(len(member.spectra))

    first = archive[0]                        # or archive["runA.msz"]
    psms = archive.get_annotation(0, "annotations/runA.pin")

    archive.decompress("out/")                # every member -> out/*.mzML
```

Members are opened on demand and released together by `close()`, so a
1000-member archive doesn't consume 1000 file descriptors unless you touch
every member.

### Reading either version uniformly

`MSZXBatchFile` also opens a **v1** archive, as a collection of one. Use it when
you're walking a directory of mixed archives and don't want to branch:

```python
from mscompress.mszx import MSZXBatchFile

with MSZXBatchFile.open(path) as archive:     # v1 -> 1 member, v2 -> N
    for member in archive:
        ...
```

`read()` is unchanged: a v1 archive still returns an `MSZXFile` (which *is* an
`MSZFile`, so `archive.spectra` works directly). Opting into the collection view
is explicit.

## Reproducibility

Batch archives contain no timestamps, so the same inputs and settings produce
byte-identical output — and the CLI, Python, and Node all drive the same C
writer, so it doesn't matter which one wrote the file.

The output must be a seekable regular file; writing to a pipe or stdout is an
error. See [why](../../format/mszx.md#why-v2-output-must-be-seekable).

## Manifest fields

See the [MSZX format spec](../../format/mszx.md) for both JSON schemas.

## When to use it

- You're shipping data to a collaborator and want them to get spectra +
  identifications in one file.
- You're building an ML training dataset where each example needs to join
  a spectrum with a peptide assignment.
- You want a self-describing artifact with provenance baked in
  (`description`, `extra`).
- You're moving a whole cohort as a single reproducible file (batch).
