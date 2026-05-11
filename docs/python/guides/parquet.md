# Parquet I/O

Round-trip between MSZ/MSZX and Parquet tables. Requires the `parquet`
extra:

```bash
pip install 'mscompress[parquet]'
```

## MSZ → Parquet

```python
from mscompress import to_parquet

to_parquet("run.msz", "run.parquet")
```

Each spectrum becomes a row; m/z and intensity arrays land in list columns.

## Parquet → MSZ (or MSZX, or TSV)

```python
from mscompress import from_parquet

from_parquet("run.parquet", "run.msz")     # produces MSZ
from_parquet("run.parquet", "run.mszx")    # produces MSZX
from_parquet("run.parquet", "run.tsv")     # produces TSV peak list
```

Output format is dispatched by the destination file extension.

## When to use it

Parquet is useful when you want columnar analytics over many spectra
(Polars, DuckDB, Spark) without writing a custom mzML/MSZ reader on every
engine. The on-disk size is generally larger than MSZ — use MSZ as the
canonical archival form and materialize Parquet when you need it.
