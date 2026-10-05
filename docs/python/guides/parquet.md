# Parquet I/O

mscompress can round-trip mass-spectrometry data between its native
formats (mzML, MSZ, MSZX) and columnar [Apache Parquet](https://parquet.apache.org/)
tables. This lets you run columnar analytics over many spectra with engines
such as Polars, DuckDB, or Spark without writing a bespoke mzML/MSZ reader
for each one, while keeping MSZ as the compact canonical archive.

Parquet support is an optional extra backed by
[`pyarrow`](https://arrow.apache.org/docs/python/):

```bash
pip install 'mscompress[parquet]'
```

Two functions form the public API:

| Function | Direction | Output |
|----------|-----------|--------|
| `to_parquet` | mzML / MSZ / MSZX → Parquet | `.parquet` |
| `from_parquet` | Parquet → MSZ / MSZX / TSV | dispatched by extension |

See the [API reference](../reference/parquet.md) for the full signatures.

## Exporting to Parquet

`to_parquet` accepts a path (mzML, `.msz`, or `.mszx`) or an already-open
`MZMLFile` / `MSZFile` / `MSZXFile`, and writes one row per spectrum:

```python
from mscompress import to_parquet

to_parquet("run.msz", "run.parquet")
```

The output uses a fixed, stable schema designed to feed back into
`from_parquet` without loss:

| Column | Arrow type | Notes |
|--------|------------|-------|
| `scan` | `int32` | Spectrum scan number |
| `ms_level` | `int16` | MS level (1, 2, …) |
| `ret_time` | `float32` | Retention time, minutes |
| `precursor` | `float32` | Precursor (selected ion) m/z |
| `charge` | `int32` | Precursor charge state |
| `mz` | `list<float32>` | m/z array |
| `intensity` | `list<float32>` | Intensity array |
| `n_peaks` | `int32` | Length of the m/z / intensity arrays |

When the source is an MSZX archive with PSM annotations, four additional
columns are appended: `peptide` (`string`), `peptide_charge` (`string`,
formatted `<peptide>_<charge>`), `score` (`float32`), and `q_value`
(`float32`).

m/z and intensity are deliberately stored as **`float32`**. This keeps the
file compact and preserves a bit-exact round-trip with `from_parquet`,
which expects 32-bit binary arrays. Retention time is auto-detected from the
first spectrum's `MS:1000016` cvParam and emitted in minutes. Scans with
multiple PSMs are reduced according to the `multi_psm` argument
(`"best"`, `"first"`, or `"all"`); see the
[API reference](../reference/parquet.md) for the full parameter list.

## Importing from Parquet

`from_parquet` converts a Parquet table into an MSZ file, an MSZX archive,
or a Percolator-style annotations TSV. The output format is chosen from the
destination extension (override with `output_type=`):

```python
from mscompress import from_parquet

from_parquet("run.parquet", "run.msz")     # MSZ spectra file
from_parquet("run.parquet", "run.mszx")    # MSZX archive (spectra + annotations)
from_parquet("run.parquet", "run.tsv")     # Percolator-style peak/PSM TSV
```

Internally the importer synthesizes a minimal mzML document from the table
and compresses it through the standard pipeline, so the resulting MSZ is
indistinguishable from one produced directly from mzML.

## Parquet input requirements

The importer is schema-flexible: it resolves logical fields by trying a set
of column-name aliases (first match wins, case-sensitive) rather than
demanding exact names. It supports two physical layouts, distinguished
automatically by the Arrow type of the `mz`/`intensity` columns.

### Required columns

**Only `mz` and `intensity` are mandatory.** They must both be present
(under any alias) and must both be the *same kind*: either both Arrow list
columns (wide layout) or both scalar numeric columns (long layout). Mixing
the two — a list `mz` with a scalar `intensity`, or vice versa — is rejected.

| Logical field | Accepted aliases |
|---------------|------------------|
| m/z | `mz`, `m/z`, `MZ`, `M/Z`, `mass_to_charge` |
| intensity | `intensity`, `intensities`, `int`, `Intensity`, `INTENSITY` |

m/z and intensity values should be **`float32`**. Other numeric types are
accepted but are cast on the way in; staying at `float32` guarantees a
bit-exact round-trip and the smallest output.

### Wide layout — one row per spectrum

When `mz` and `intensity` are **list columns**, each row is treated as a
complete spectrum. This is the layout `to_parquet` emits. Every spectrum is
written as an MS2 spectrum with a sequentially assigned scan number.

All remaining columns are optional and filled with defaults when absent:

| Logical field | Accepted aliases | Type | Default |
|---------------|------------------|------|---------|
| retention time | `ret_time`, `retention_time`, `rt`, `RT` | float | `-1.0` |
| precursor m/z | `precursor`, `precursor_mz`, `Precursor` | float | `0.0` |
| charge | `charge`, `Charge`, `z`, `precursor_charge` | int | `0` |
| peptide | `peptide`, `Peptide`, `sequence` | string | `""` |
| peptide + charge (combined) | `peptide_charge`, `peptide.charge`, `PeptideCharge` | string | — |
| score | `score`, `Score`, `max_score`, `mean_score` | float | empty |

The `ret_time` column is interpreted in minutes by default; pass
`ret_time_unit="second"` to `from_parquet` if it is stored in seconds.

### Long layout — one row per peak

When `mz` and `intensity` are **scalar numeric columns** (one peak per row,
as produced by ThermoRawFileParser `.mzparquet`), rows are aggregated into
spectra by scan id. This layout has two additional **required** columns:

| Logical field | Accepted aliases | Requirement |
|---------------|------------------|-------------|
| scan id | `scan`, `scan_number`, `scanNumber`, `scan_id` | required (groups peaks into spectra) |
| MS level | `level`, `ms_level`, `msLevel` | required (determines MS1 vs MSn output) |

Rows for a single scan are expected to be contiguous (all peaks for one scan
in the same row group). Per-scan metadata is read from the first row of each
scan's run. Long-layout-only optional columns are also recognized:

| Logical field | Accepted aliases | Default |
|---------------|------------------|---------|
| retention time | `ret_time`, `retention_time`, `rt`, `RT` | `-1.0` |
| precursor m/z | `precursor`, `precursor_mz`, `Precursor` | `0.0` |
| charge | `charge`, `Charge`, `z`, `precursor_charge` | `0` |
| ion mobility | `ion_mobility`, `ionMobility`, `drift_time` | omitted |
| isolation window lower | `isolation_lower`, `isolationLower` | omitted |
| isolation window upper | `isolation_upper`, `isolationUpper` | omitted |

Long-format tables carry no PSM columns, so `from_parquet(..., "out.mszx")`
produces an archive without an annotations bundle, and TSV export
(`from_parquet(..., "out.tsv")`) is not supported for them.

### Combined `peptide_charge` columns

If a table stores the peptide and its charge together in one string column
(formatted `<peptide>_<charge>`, e.g. `PEPTIDE_2`), name it `peptide_charge`
(or `peptide.charge` / `PeptideCharge`). It is consulted only when a
standalone `peptide` or `charge` column is absent, and is split on the *last*
underscore so peptides containing modification underscores
(`_(Acetyl)PEPTIDE_2`) still parse. A non-numeric charge suffix falls back
to a charge of `0`.

### Score selection

For MSZX and TSV output the PSM score column can be named explicitly with
`score_column=`. If omitted, the first present of `max_score`, `mean_score`,
`score`, or `Score` is used; if none exist, score cells are left empty
(downstream readers treat this as `0.0`). Passing a `score_column` that is
not in the schema raises an error.

## Choosing between MSZ and Parquet

Parquet is the right tool when you want engine-agnostic columnar access to
many spectra — filtering, aggregation, or feeding ML pipelines from Polars,
DuckDB, Spark, or pandas. The on-disk footprint is generally larger than
MSZ, so treat MSZ (or MSZX) as the canonical archival form and materialize
Parquet on demand.
