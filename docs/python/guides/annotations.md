# Search results

`mscompress.annotations` reads peptide-spectrum matches (PSMs) produced by
common search and rescoring tools into a single `PSMTable`: a pandas
DataFrame with a fixed set of core columns, plus the `SearchMetadata` of the
search that produced them. Downstream code works with the same columns
whatever the source format.

See the [API reference](../reference/annotations.md) for full signatures.

## Reading any format

`read_psms` detects the format from the file name, then from its contents,
and reads it with default options:

```python
from mscompress.annotations import read_psms

table = read_psms("results.pepXML")
print(table)                             # PSMTable(1234 PSMs from MSFragger)
confident = table.df[table.df.q_value < 0.01]
```

| Format | Class | Extensions | Sniffed by |
|--------|-------|------------|------------|
| mzTab 1.0 | `MzTab` | `.mztab` | `MTD` / `COM` lines, or a `PSH` header |
| pepXML | `PepXML` | `.pepxml`, `.pep.xml` | `<msms_pipeline_analysis>` / `<spectrum_query>` |
| Percolator output / `.pin` | `Percolator` | `.pin`, `.tsv` | first column `PSMId` / `SpecId` |

A trailing `.zst` is ignored for detection and the file is decompressed
transparently, so `results.pin.zst` reads as Percolator. `read_psms` also
accepts the file's contents as `bytes`. To skip detection, pass the format:

```python
from mscompress.types import AnnotationFormat

table = read_psms("results.dat", format=AnnotationFormat.PERCOLATOR_TSV)
```

## Using a specific format

Format-specific options are constructor arguments; `read()` takes a path or
bytes:

```python
from mscompress.annotations import PepXML, Percolator

table = PepXML(max_rank=2).read("search.pep.xml")
table = Percolator(decoy_prefix="rev_").read("results.pin")
```

| Format | Option | Default | Purpose |
|--------|--------|---------|---------|
| `PepXML` | `max_rank` | `1` | Keep hits with `hit_rank <= max_rank` |
| `PepXML` | `decoy_prefix` | file's `decoy_prefix` parameter, else `"DECOY_"` | Protein prefix marking decoys |
| `Percolator` | `decoy_prefix` | `"DECOY_"` | Protein prefix marking decoys (used only when there is no `Label` column) |

## The PSM table

`table.df` has these core columns first, in this order (`PSM_COLUMNS`, with
dtypes in `PSM_SCHEMA`):

| Column | dtype | Notes |
|--------|-------|-------|
| `scan` | Int64 | Scan number the PSM was identified in; joins to `Spectrum.scan` |
| `peptide` | string | Bare amino-acid sequence |
| `modifications` | string | mzTab notation, e.g. `1-UNIMOD:35,5-UNIMOD:4` |
| `charge` | Int64 | Precursor charge |
| `score` | float64 | Primary score, named by `metadata.primary_score` |
| `q_value` | float64 | |
| `pep` | float64 | Posterior error probability |
| `retention_time` | float64 | Seconds |
| `precursor_mz` | float64 | Observed |
| `calc_mz` | float64 | Theoretical |
| `proteins` | object | List of protein accessions |
| `pre` / `post` | string | Residues before / after the peptide |
| `is_decoy` | bool | |

Each format adds its own columns after these — e.g. pepXML adds `rank`,
`modified_peptide`, `mass_diff`, ion counts and one column per
`search_score`; Percolator keeps every feature column. Core columns a format
doesn't provide are NA.

Look PSMs up by scan:

```python
table.scans                    # scan numbers with at least one PSM, ascending
table.has_scan(1234)           # bool
table.for_scan(1234)           # DataFrame of that scan's PSMs (may be empty)
table.records_for_scan(1234)   # list of row dicts (fast in per-spectrum loops)
subset = table.filter_scans([1234, 1235])   # new PSMTable
```

`table.metadata` is a `SearchMetadata` (software and version, primary score
name, fixed and variable `ModificationSpec`s, source format and file).

## Writing mzTab

MSZX archives store search results as mzTab. `MzTab().write` serializes a
table, metadata included, and returns the document (optionally writing it to
a file); `MzTab().read` reads it back:

```python
from mscompress.annotations import MzTab

MzTab().write(table, "results.mztab")
same = MzTab().read("results.mztab")
```

`MzTab` reads the layout `write` produces; mzTab from other tools is not
supported.

## Search results in MSZX

`MSZXBuilder.add_psms` (and `create_mszx(psms=[...])`) accepts a `PSMTable`
or a path to any supported file and stores it as mzTab. When reading an
archive, the stored tables are parsed on open:

```python
from mscompress.mszx import MSZXFile

with MSZXFile.open("run.mszx") as archive:
    table = archive.psms                 # first PSMTable, or None
    archive.psm_tables                   # {member name: PSMTable}
    table = archive.get_psms("results.pepXML.mztab.zst")
```

## Joining PSMs to spectra

Look each spectrum's PSMs up by scan as you iterate:

```python
with MSZXFile.open("run.mszx") as archive:
    table = archive.psms
    for s in archive.spectra:
        for psm in table.records_for_scan(s.scan):
            if psm["q_value"] < 0.01:
                ...
```

To keep only the best PSM per scan, reduce the DataFrame first:

```python
df = table.df
best = df.loc[df.groupby("scan")["score"].idxmax()]
```

For streaming joins over larger archives, see the
[ML dataset adapters](ml-datasets.md).
