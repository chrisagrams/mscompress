# MSZX archives

Bundle an MSZ-compressed spectra file with search results into a single
self-describing archive.

## Build

```python
from mscompress import MSZXBuilder, read

with read("spectra.msz") as msz:
    (
        MSZXBuilder(msz)
            .set_description("DDA run #42")
            .add_psms("results.pin", description="Percolator")
            .add_psms("search.pep.xml", description="Comet")
            .set_extra("instrument", "Orbitrap Eclipse")
            .save("run42.mszx")
    )
```

`add_psms` takes a `PSMTable` or a pepXML / Percolator / mzTab file (see
[Search results](annotations.md)) and stores it as mzTab, zstd-compressed by
default (`MSZXBuilder(msz, compression=False)` to store it plain). The member
is named after the source file — `results.pin` becomes `results.pin.mztab.zst`;
pass `name=` to choose another.

## Or use the convenience function

```python
from mscompress import create_mszx, read

with read("spectra.msz") as msz:
    create_mszx(
        msz,
        "run42.mszx",
        psms=["results.pin", "search.pep.xml"],
        description="DDA run #42",
    )
```

## Read

```python
from mscompress import MSZXFile

with MSZXFile.open("run42.mszx") as archive:
    print(archive.manifest.description)
    print(archive.manifest.num_spectra)

    # Iterate spectra
    for s in archive.spectra:
        ...

    # Search results, parsed on open
    table = archive.psms                          # first PSMTable, or None
    for name, table in archive.psm_tables.items():
        print(name, table.metadata.software, len(table))
        print(table.df[["scan", "peptide", "score"]].head())
```

See [Search results](annotations.md#joining-psms-to-spectra) for joining PSMs
to spectra.

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

### Incremental control, with search results

```python
from mscompress import MSZXBatchWriter

with MSZXBatchWriter("cohort.mszx", threads=8) as writer:
    for path in mzml_paths:
        idx = writer.add(path)                      # -> entry index
        member = writer.add_psms(idx, path.with_suffix(".pin"))
        # -> "<stem>.pin.mztab.zst"
```

Leaving the `with` block via an exception calls `abort()`, so a partial archive
is never left behind. `add()` also accepts an already-open `MZMLFile`, reusing
its mapping rather than re-opening the file.

`add_psms` takes a `PSMTable` or a pepXML / Percolator / mzTab file and stores
it as mzTab, zstd-compressed unless `compressed=False`. To attach some other
file verbatim, use `writer.add_annotation(idx, data, filename, format=...)`;
those payloads are stored as given — compress them yourself and pass
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
    psms = archive.get_psms(0, "runA.pin.mztab.zst")   # -> PSMTable
    raw = archive.get_annotation(0, "runA.pin.mztab.zst")  # bytes, as stored

    archive.decompress("out/")                # every member -> out/*.mzML
```

Members are opened on demand and released together by `close()`, so a
1000-member archive doesn't consume 1000 file descriptors unless you touch
every member.

### Query the spectrum index

Batch archives carry a [per-spectrum index](../../format/mszx.md#spectrum-index)
— scan, MS level, RT, precursor m/z, charge, TIC, and base peak m/z for every
spectrum in every member. `query()` reads only the index, so finding spectra
across a 1000-run cohort opens no member:

```python
with read("cohort.mszx") as archive:
    hits = archive.query(rt_sec=(600, 660), ms_level=2, precursor_mz=(500, 510))
    print(len(hits))

    df = hits.to_frame()        # metadata only, entry_name column first
    for spectrum in hits.spectra():
        ...                     # decompresses just the matching spectra
```

Each keyword is a column. A value tests equality; a `(lo, hi)` tuple tests an
inclusive range, with `None` for an open side (`rt_sec=(600, None)`). All terms
must match, and missing values never do. `entry` also takes a member name:

```python
archive.query(entry="runA.msz", charge=(2, 3))
```

`hits.spectra()` groups hits by member, so each member is opened once.
`hits.to_numpy()` returns a structured array instead of a DataFrame, and
`hits.rows` the matching row numbers.

For whole columns, use the index directly — `archive.index` is a
`SpectrumIndex` memory-mapped on first access:

```python
rt = archive.index.column("rt_sec")           # read-only numpy array
table = archive.index.to_numpy()              # every row
```

The index is written by default. Pass `index=False` to `compress_batch()` or
`MSZXBatchWriter` to leave it out; `archive.has_index` tells you whether an
archive has one, and `query()` raises `ValueError` when it doesn't.

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
