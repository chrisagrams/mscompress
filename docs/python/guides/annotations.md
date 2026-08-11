# Annotation readers

`mscompress.annotations` reads peptide-spectrum match (PSM) files produced
by common search and rescoring tools and normalizes them into a single
`PSM` dataclass. Every reader exposes the same interface — iteration,
indexing, and scan-number lookup — so downstream code never has to branch
on the source format.

The package handles two concerns:

- **Readers** parse a PSM file into `PSM` objects. `TSVReader`
  (Percolator `.pin` / `.tsv`) and `PepXMLReader` (pepXML) are the concrete
  implementations; `PSMReader` is a factory that picks one for you.
- **Annotation files** (`PathAnnotationFile`, `MSZXAnnotationFile`) abstract
  *where* the bytes come from — a file on disk, or a member inside an MSZX
  archive — with transparent zstd decompression.

See the [API reference](../reference/annotations.md) for full signatures.

## Auto-detecting the format

`PSMReader` is a factory: it inspects the filename, then the file contents
if the extension is ambiguous, and returns the appropriate concrete reader.

```python
from mscompress.annotations import PSMReader

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
| `.pin`, `.tsv` | Percolator TSV (`TSVReader`) |
| `.pepxml`, `.pep.xml` | pepXML (`PepXMLReader`) |
| `.xml`, unknown | content sniffed (`pepXML` / `spectrum_query` → pepXML; `SpecId` / `PSMId` / `ScanNr` → Percolator) |

A trailing `.zst` is stripped before detection, so `results.pin.zst`
resolves to Percolator TSV and is decompressed transparently. When the
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
from mscompress.annotations import TSVReader, PepXMLReader

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

## Reading annotations bundled in MSZX

MSZX archives can carry one or more annotation files alongside the spectra.
Open the archive with `MSZXFile.open` (or the generic `read`) and access the
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
with MSZXFile.open("run.mszx") as archive:
    archive.annotation_files                       # list of manifest entries
    archive.annotation_readers                     # {filename: reader}

    pin = archive.get_annotation_reader("results.pin.zst")
    pepxmls = archive.get_annotation_readers_by_format("pepxml")
```

`archive.annotations` returns the primary reader (the first bundled
annotation), or `None` for archives that contain only spectra.

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
