# MSZX archives

Bundle an MSZ-compressed spectra file with search results into a single
self-describing archive.

Search results are stored **only as mzTab** (1.0, PSM section). Engine output
— pepXML or Percolator TSV/PIN — is converted when the archive is written and
the original file is not stored; mzTab input is stored unchanged. See the
[format spec](../../format/mszx.md#search-results-mztab) for the layout.

## Build

```python
from mscompress import MSZXBuilder, read
from mscompress.annotations import PSMReader

msz = read("spectra.msz")
(
    MSZXBuilder(msz)                                  # compression=True by default
        .add_annotations(PSMReader("results.pin"))     # -> results.pin.mztab.zst
        .add_annotations(PSMReader("search.pep.xml"),  # -> search.pep.xml.mztab.zst
                         description="Comet search")
        .set_description("DDA run #42")
        .set_extra("instrument", "Orbitrap Eclipse")
        .save("run42.mszx")
)
```

Each annotation becomes one member named `<original file name>.mztab`, plus
`.zst` when the builder compresses (the default). The manifest entry's
`format` is `"mztab"`, and the builder's default `join_key` is
`"spectra_ref"` (the mzTab column); change it with `set_join_key()`.

PSMs you already hold in memory can be written directly with `add_psms`:

```python
from mscompress.annotations import SearchInfo

builder.add_psms(
    psms,                                   # list[PSM]
    "rescored",                             # -> rescored.mztab.zst
    search_info=SearchInfo(software="MyRescorer", primary_score_name="score"),
    description="Rescored PSMs",
)
```

## Or use the convenience function

```python
from mscompress import create_mszx, read

create_mszx(
    read("spectra.msz"),
    "run42.mszx",
    annotations=["results.pin", "search.pep.xml"],   # paths or readers
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

    # Primary search results (the first mzTab entry), or None
    for psm in archive.annotations:
        print(psm.scan_number, psm.peptide, psm.score, psm.q_value)

    # Every annotation, keyed by member name
    for name, reader in archive.annotation_readers.items():
        print(name, len(reader), reader.search_info.software)
```

Extracting a subset to a new `.mszx` (`archive.extract("subset.mszx",
scan_numbers=[...])`) filters each annotation to the PSMs of the extracted
scans and writes it as mzTab.

See [Annotation readers](annotations.md) for the reader interface.

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
        writer.add_psms(idx, path.with_suffix(".pin"))  # -> <stem>.pin.mztab.zst
        writer.set_join_key(idx, "spectra_ref")
```

Leaving the `with` block via an exception calls `abort()`, so a partial archive
is never left behind. `add()` also accepts an already-open `MZMLFile`, reusing
its mapping rather than re-opening the file.

`add_psms(entry_index, source, filename=None, compressed=True)` takes a results
file path or a `BasePSMReader`, converts it to mzTab, zstd-compresses it unless
`compressed=False`, and returns the member name written (`<filename or source
name>.mztab`, plus `.zst`). Use it for search results.

The lower-level `add_annotation()` stores a payload exactly as given — compress
it yourself and pass `compressed=True` if you want it zstd-compressed. It does
not convert, so pass mzTab bytes with `format="mztab"` to stay consistent with
the other writers.

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
    raw = archive.get_annotation(0, "runA.pin.mztab.zst")   # bytes, as stored

    archive.decompress("out/")                # every member -> out/*.mzML
```

`get_annotation()` returns the member's bytes as stored (still
zstd-compressed if the manifest says so); pass them to `MzTabReader` after
decompressing to get PSMs.

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

## Upgrading older archives

Archives written before mzTab became the only stored format contain the
engine's own files (pepXML, Percolator TSV/PIN). They still open and read
normally — legacy members are parsed with `PepXMLReader` / `TSVReader` — but
can be converted so every annotation is mzTab:

```python
from mscompress.mszx import upgrade_mszx

upgrade_mszx("old.mszx")                         # rewrite in place
upgrade_mszx("old.mszx", output="new.mszx")      # or write a copy
```

or from the shell:

```bash
python -m mscompress.mszx.upgrade old.mszx [-o new.mszx]
```

Each non-mzTab member is converted, renamed to `<name>.mztab` (keeping its
`.zst` and directory prefix if it had them), and the original is dropped; the
manifest entries and `join_key` (`"spectra_ref"`) are updated. Spectra and any
other members are copied byte-for-byte. Both v1 and v2 archives are supported,
and an archive that is already all-mzTab is left as is (or copied unchanged
to `output`).

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
