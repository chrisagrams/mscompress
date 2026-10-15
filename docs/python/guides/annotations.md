# Annotation readers

`mscompress.annotations` reads peptide-spectrum match (PSM) files produced
by common search and rescoring tools and normalizes them into a single
`PSM` dataclass. Every reader exposes the same interface — iteration,
indexing, and scan-number lookup — so downstream code never has to branch
on the source format.

The package handles three concerns:

- **Readers** parse a PSM file into `PSM` objects. `MzTabReader` (mzTab),
  `TSVReader` (Percolator `.pin` / `.tsv`) and `PepXMLReader` (pepXML) are the
  concrete implementations; `PSMReader` is a factory that picks one for you.
- **mzTab conversion** (`write_mztab`, `reader_to_mztab`) turns PSMs from any
  reader into mzTab 1.0 — the only format MSZX archives store search results
  in.
- **Annotation files** (`PathAnnotationFile`, `MSZXAnnotationFile`) abstract
  *where* the bytes come from — a file on disk, or a member inside an MSZX
  archive — with transparent zstd decompression.

See the [API reference](../reference/annotations.md) for full signatures.

## Auto-detecting the format

`PSMReader` is a factory: it inspects the filename, then the file contents
if the extension is ambiguous, and returns the appropriate concrete reader.

```python
from mscompress.annotations import PSMReader

with PSMReader("results.mztab") as reader:      # mzTab
    for psm in reader:
        print(psm.scan_number, psm.peptide, psm.q_value)

with PSMReader("results.pin") as reader:        # Percolator TSV
    for psm in reader:
        print(psm.scan_number, psm.peptide, psm.score)

with PSMReader("search.pep.xml") as reader:     # pepXML
    for psm in reader:
        ...
```

Detection rules:

| Extension | Resolved format |
|-----------|-----------------|
| `.mztab` | mzTab (`MzTabReader`) |
| `.pin`, `.tsv` | Percolator TSV (`TSVReader`) |
| `.pepxml`, `.pep.xml` | pepXML (`PepXMLReader`) |
| `.xml`, unknown | content sniffed (leading `MTD` line or a `PSH` header → mzTab; `pepXML` / `spectrum_query` → pepXML; `SpecId` / `PSMId` / `ScanNr` → Percolator) |

A trailing `.zst` is stripped before detection, so `results.pin.zst`
resolves to Percolator TSV (and `results.pin.mztab.zst` to mzTab) and is decompressed transparently. When the
format still cannot be determined, pass it explicitly with the
`AnnotationFormat` enum:

```python
from mscompress.types import AnnotationFormat

with PSMReader("results.dat", format=AnnotationFormat.PERCOLATOR_TSV) as reader:
    ...
```

## Using a specific reader

If you already know the format, construct the reader directly. The concrete
readers accept format-specific options that the factory does not surface.

```python
from mscompress.annotations import MzTabReader, TSVReader, PepXMLReader

# mzTab: also exposes the raw MTD section and the conversion provenance
reader = MzTabReader("results.mztab")
reader.metadata          # {"mzTab-version": "1.0.0", ...}
reader.source_format     # e.g. "pepxml" if converted by mscompress, else None
reader.source_name       # e.g. "search.pep.xml"

# Percolator: customize the decoy-protein prefix
with TSVReader("results.pin", decoy_prefix="rev_") as reader:
    targets = [p for p in reader if p.is_target]

# pepXML: keep only rank-1 hits (the default), or widen the cutoff
with PepXMLReader("search.pep.xml", min_rank=1) as reader:
    ...
```

Reader options:

| Reader | Option | Default | Purpose |
|--------|--------|---------|---------|
| `TSVReader` | `decoy_prefix` | `"DECOY_"` | Protein-name prefix marking decoys (used only when no `Label` column is present) |
| `PepXMLReader` | `min_rank` | `1` | Include only hits with `rank <= min_rank` |
| `PepXMLReader` | `decoy_prefix` | `"DECOY_"` | Decoy protein prefix |

## The reader interface

All readers share the `BasePSMReader` interface. Parsing is lazy and cached:
the file is read on first access (iteration, `len()`, indexing, or any
lookup), then served from memory thereafter.

```python
with PSMReader("results.pin") as reader:
    n = len(reader)                       # parse + count
    first = reader[0]                     # index
    page = reader[10:20]                  # slice -> list[PSM]
    for psm in reader:                    # iterate
        ...

    scans = reader.scan_numbers           # unique scan numbers with PSMs
    hits = reader.get_by_scan(1234)       # list[PSM] for a scan (may be empty)
    best = reader.get_best_by_scan(1234)  # highest-scoring PSM, or None
    present = reader.has_scan(1234)        # bool
    all_psms = reader.psms                # list[PSM]
    info = reader.search_info             # SearchInfo (software, scores, mods)
```

`get_by_scan` returns *every* PSM for a scan (a scan may have multiple
candidate peptides); `get_best_by_scan` collapses them to the maximum
`score`.

## The `PSM` dataclass

Each reader emits `PSM` instances. The first four fields are always
populated; the rest are optional and default to `None` (or sensible
defaults) when the source format does not provide them.

| Field | Type | Notes |
|-------|------|-------|
| `scan_number` | int | Joins to `Spectrum.scan` |
| `peptide` | str | Sequence, modifications stripped (flanking residues like `K.PEPTIDE.R` removed) |
| `charge` | int | Precursor charge |
| `score` | float | Primary search/scoring metric |
| `proteins` | list[str] | Mapped protein accessions |
| `spectrum_index` | int \| None | Zero-based spectrum index, if known |
| `q_value` | float \| None | FDR-controlled q-value (Percolator) |
| `pep` | float \| None | Posterior error probability |
| `retention_time` | float \| None | Seconds |
| `precursor_mz` | float \| None | m/z |
| `precursor_mass` | float \| None | Neutral mass |
| `mass_diff` | float \| None | Observed − calculated mass |
| `modified_peptide` | str \| None | Sequence with modification notation |
| `num_matched_ions` | int \| None | Matched fragment ions |
| `num_total_ions` | int \| None | Theoretical fragment ions |
| `rank` | int | Hit rank (1 = best); defaults to `1` |
| `is_decoy` | bool | Decoy flag; `is_target` is the inverse |
| `extra` | dict | Format-specific columns kept verbatim |

Columns a reader does not map to a named field are preserved in `extra` —
for Percolator TSV, that includes every numeric feature column, so
engine-specific scores remain accessible:

```python
psm.extra["deltCn"]        # Percolator feature
psm.extra.get("hyperscore")
```

For Percolator output, `q_value` and `pep` are filled from the `q-value` and
`posterior_error_prob` columns when present (they also remain in `extra`).

## Search metadata

`reader.search_info` returns a `SearchInfo` describing the search as a whole,
populated where the source format records it:

| Field | Type | Notes |
|-------|------|-------|
| `software` | str \| None | Search engine / rescoring tool (pepXML `search_engine`; `"Percolator"` for Percolator output) |
| `software_version` | str \| None | Version string, if recorded |
| `primary_score_name` | str \| None | Name of the score stored in `PSM.score` (e.g. `"hyperscore"`, `"xcorr"`, `"score"`) |
| `fixed_mods` | list[ModificationSpec] | Fixed modifications searched |
| `variable_mods` | list[ModificationSpec] | Variable modifications searched |

A `ModificationSpec` has `massdiff` (Da), `site` (residue letter, `"N-term"`
or `"C-term"`) and `position` (`"Anywhere"`, `"Any N-term"`, `"Any C-term"`,
`"Protein N-term"`, `"Protein C-term"`). This is the metadata written into the
`MTD` section when PSMs are converted to mzTab.

## Converting to mzTab

MSZX archives store search results only as mzTab, and the builders convert
for you (see [MSZX archives](mszx.md)). To produce mzTab yourself:

```python
from mscompress.annotations import PSMReader, reader_to_mztab, write_mztab, SearchInfo

# Any reader -> mzTab text (mzTab input is returned unchanged)
text = reader_to_mztab(PSMReader("search.pep.xml"), ms_run_location="run.msz")

# In-memory PSMs -> mzTab text
text = write_mztab(
    psms,
    search_info=SearchInfo(software="Comet", primary_score_name="xcorr"),
    ms_run_location="run.msz",
)
```

`write_mztab(psms, search_info=None, ms_run_location=None, source_format=None,
source_name=None, title=None)` returns the document as a string;
`reader_to_mztab(reader, ms_run_location=None, title=None)` fills
`search_info`, `source_format` and `source_name` from the reader. The layout
is described in the [MSZX format spec](../../format/mszx.md#search-results-mztab);
the Node.js binding writes and reads the same files.

## Reading annotations bundled in MSZX

MSZX archives can carry one or more annotation files alongside the spectra,
stored as mzTab. Open the archive with `MSZXFile.open` (or the generic `read`) and access the
bundled readers through the archive — no manual extraction or decompression
needed.

```python
from mscompress.mszx import MSZXFile

with MSZXFile.open("run.mszx") as archive:
    reader = archive.annotations          # primary reader, or None
    if reader is not None:
        for psm in reader:
            ...
```

When an archive bundles several annotation files, address them by name or
by format:

```python
from mscompress.types import AnnotationFormat

with MSZXFile.open("run.mszx") as archive:
    archive.annotation_files                       # list of manifest entries
    archive.annotation_readers                     # {filename: reader}

    pin = archive.get_annotation_reader("results.pin.mztab.zst")
    mztabs = archive.get_annotation_readers_by_format(AnnotationFormat.MZTAB)
```

`archive.annotations` returns the primary reader — the first mzTab entry —
or `None` for archives that contain only spectra.

Archives written before mzTab became the only stored format carry the
engine's own files (pepXML, Percolator TSV/PIN). These are still readable:
each member is opened with its legacy reader (`PepXMLReader` / `TSVReader`)
and exposes the same interface, and `archive.annotations` falls back to the
first of them when the archive has no mzTab entry.

## Joining PSMs to spectra

The common pattern is to index PSMs by scan number, then look each spectrum
up as you iterate:

```python
with MSZXFile.open("run.mszx") as archive:
    psms_by_scan = {
        psm.scan_number: psm
        for psm in archive.annotations
    }
    for s in archive.spectra:
        psm = psms_by_scan.get(s.scan)
        if psm and psm.q_value is not None and psm.q_value < 0.01:
            ...
```

If a scan can have multiple PSMs and you only want the best, build the index
with `get_best_by_scan` instead:

```python
reader = archive.annotations
best_by_scan = {
    scan: reader.get_best_by_scan(scan)
    for scan in reader.scan_numbers
}
```

For larger archives where a full in-memory index is undesirable, build the
join lazily — see the [ML dataset adapters](ml-datasets.md) for grain-style
streaming joins.
