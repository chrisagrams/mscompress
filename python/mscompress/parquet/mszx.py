"""parquet -> .mszx + parquet -> annotations TSV.

`parquet_to_mszx` bundles the .msz produced by `parquet_to_msz` with a
Percolator-style annotations TSV (written here as `_write_annotations_tsv`)
into a single archive.
"""

from __future__ import annotations

import os
import tempfile
from os import PathLike
from pathlib import Path
from typing import Iterator, List, Optional, Sequence, Tuple, Union

import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq

from mscompress.annotations import TSVReader
from mscompress.mszx import MSZXBuilder

from mscompress.parquet.msz import parquet_to_msz
from mscompress.parquet.mzml import (
    _DEFAULT_CHARGE,
    _DEFAULT_PEPTIDE,
    _DEFAULT_PRECURSOR,
    _DEFAULT_RET_TIME,
    _OPTIONAL_FLOAT_COLUMNS,
    _ColumnMap,
    _resolve_schema,
    _split_peptide_charge,
)


# A per-row indexable view of one column. Either a numpy view (when the
# column exists in the parquet) or a Python list of defaults (when it doesn't).
# Both support `[i]` and the numeric casts (`int(...)`, `float(...)`) the row
# loop performs; the row loop never needs to know which it got.
_NumColumn = Union[np.ndarray, List[Union[int, float]]]


def _tsv_escape(value: str) -> str:
    """Strip whitespace that would break a TSV row."""
    return value.replace("\t", " ").replace("\n", " ").replace("\r", " ")


def _col_or_default(
    batch: pa.RecordBatch,
    name: Optional[str],
    default: Union[int, float],
) -> _NumColumn:
    """Numpy view of a numeric column, or `[default] * num_rows` when absent.

    Lets the row loop index uniformly without per-column `if col is None` checks.
    """
    if name is not None:
        return batch.column(name).to_numpy(zero_copy_only=False)
    return [default] * batch.num_rows


def _resolve_peptide_charge(
    cmap: _ColumnMap,
    batch: pa.RecordBatch,
) -> Tuple[List[str], _NumColumn]:
    """Return per-row (peptides, charges) for a batch.

    Handles the three peptide-source shapes the loader accepts:
      1. separate `peptide` column (+ optional standalone `charge`)
      2. combined `peptide_charge` string column ("PEPTIDE_2")
      3. neither (peptide defaults, charge from standalone column or default)
    """
    n = batch.num_rows
    if cmap.peptide is not None:
        peptides: List[str] = [
            p or _DEFAULT_PEPTIDE
            for p in batch.column(cmap.peptide).to_pylist()
        ]
        return peptides, _col_or_default(batch, cmap.charge, _DEFAULT_CHARGE)
    if cmap.peptide_charge is not None:
        peptides = []
        charges: List[Union[int, float]] = []
        for s in batch.column(cmap.peptide_charge).to_pylist():
            p, c = _split_peptide_charge(s or "")
            peptides.append(p)
            charges.append(c)
        return peptides, charges
    return [_DEFAULT_PEPTIDE] * n, _col_or_default(batch, cmap.charge, _DEFAULT_CHARGE)


def _batch_to_tsv_rows(
    batch: pa.RecordBatch,
    cmap: _ColumnMap,
    extra_cols: Sequence[str],
    *,
    start_scan: int,
) -> Iterator[str]:
    """Yield one TSV-formatted line per row in `batch`, beginning at start_scan."""
    peptides, charges = _resolve_peptide_charge(cmap, batch)
    precursors = _col_or_default(batch, cmap.precursor, _DEFAULT_PRECURSOR)
    ret_times = _col_or_default(batch, cmap.ret_time, _DEFAULT_RET_TIME)
    scores: Optional[np.ndarray] = (
        batch.column(cmap.score).to_numpy(zero_copy_only=False)
        if cmap.score is not None else None
    )
    extras: List[np.ndarray] = [
        batch.column(c).to_numpy(zero_copy_only=False) for c in extra_cols
    ]

    for i in range(batch.num_rows):
        cells = [
            str(start_scan + i),
            _tsv_escape(peptides[i]),
            str(int(charges[i])),
            repr(float(scores[i])) if scores is not None else "",
            repr(float(precursors[i])),
            repr(float(ret_times[i])),
        ]
        cells.extend(repr(float(e[i])) for e in extras)
        yield "\t".join(cells)


def _write_annotations_tsv(
    parquet_path: Path,
    tsv_path: Path,
    *,
    score_column: Optional[str],
) -> int:
    """Write a Percolator-compatible TSV beside the spectra file.

    Returns the number of rows written.

    `score_column=None` writes empty `score` cells (callers downstream of
    `TSVReader` will see `score=0.0`). Pass an explicit column name to require
    it; if absent from the schema, `_resolve_schema` raises.
    """
    pf = pq.ParquetFile(str(parquet_path))
    schema = pf.schema_arrow
    cmap = _resolve_schema(schema, score_column=score_column)

    if cmap.is_long:
        raise ValueError(
            "TSV annotations export is not supported for long-format "
            "(.mzparquet) input: no peptide/score columns present. "
            f"Got schema columns: {schema.names}"
        )

    # Optional float columns we still surface as TSV extras when present (and
    # not already used as the score column).
    extra_cols = [
        c for c in _OPTIONAL_FLOAT_COLUMNS
        if c in schema.names and c != cmap.score
    ]
    read_cols = [c for c in (
        cmap.peptide, cmap.charge, cmap.peptide_charge,
        cmap.precursor, cmap.ret_time, cmap.score,
    ) if c is not None] + extra_cols
    headers = ["ScanNr", "Peptide", "Charge", "score", "precursor", "ret_time"] + extra_cols

    n = 0
    with open(tsv_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("\t".join(headers) + "\n")
        for batch in pf.iter_batches(columns=read_cols):
            for row in _batch_to_tsv_rows(batch, cmap, extra_cols, start_scan=n + 1):
                out.write(row + "\n")
                n += 1

    return n


def parquet_to_annotations_tsv(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    score_column: Optional[str] = None,
) -> Path:
    """Extract per-row metadata from a parquet into a Percolator-style TSV.

    Args:
        parquet_path: Source parquet.
        output_path: Destination `.tsv` path.
        score_column: Parquet column to surface as the PSM `score`. If `None`
            (default), the writer auto-picks one of `max_score`, `mean_score`,
            `score`, or `Score` if present, otherwise leaves the score cells
            empty. If you pass an explicit name and it is not in the schema,
            this raises.

    Returns:
        The written TSV path.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))
    _write_annotations_tsv(parquet_path, output_path, score_column=score_column)
    return output_path


def parquet_to_mszx(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    score_column: Optional[str] = None,
    use_zlib_binary: bool = False,
    ret_time_unit: str = "minute",
    description: Optional[str] = None,
) -> Path:
    """Bundle a parquet file into a complete `.mszx` archive.

    Produces a `.mszx` containing:
      - `spectra.msz` - compressed mz/intensity/retention-time data
      - `<source>.tsv.zst` - Percolator-style annotations (peptide/charge/score…)
      - `manifest.json`

    Args:
        parquet_path: Source parquet.
        output_path: Destination `.mszx` path.
        score_column: Parquet column to use as PSM score. `None` (default)
            auto-picks `max_score`/`mean_score`/`score`/`Score` if present,
            otherwise emits empty score cells. An explicit name that is not
            in the schema raises.
        use_zlib_binary: zlib-deflate binary arrays inside the synthesized
            mzML before base64 encoding.
        ret_time_unit: `"minute"` (default) or `"second"`.
        description: Optional human-readable description.

    Returns:
        The written archive path.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))

    # Peek at the schema: long-format input (.mzparquet) has no PSM columns,
    # so the archive is built without an annotations bundle.
    is_long = _resolve_schema(pq.ParquetFile(str(parquet_path)).schema_arrow).is_long

    with tempfile.TemporaryDirectory(prefix="mscompress-parquet-mszx-") as td_str:
        td = Path(td_str)
        tmp_msz = td / "spectra.msz"
        tmp_tsv = td / (parquet_path.stem + ".tsv")

        msz = parquet_to_msz(
            parquet_path, tmp_msz,
            use_zlib_binary=use_zlib_binary,
            ret_time_unit=ret_time_unit,
        )
        try:
            builder = MSZXBuilder(msz, source_name=parquet_path.name)
            if not is_long:
                _write_annotations_tsv(parquet_path, tmp_tsv, score_column=score_column)
                reader = TSVReader(tmp_tsv)
                builder.add_annotations(
                    reader,
                    description=description or f"Annotations extracted from {parquet_path.name}",
                )
            if description:
                builder.set_description(description)
            return builder.save(output_path)
        finally:
            # Release mmap before TemporaryDirectory cleans up tmp_msz (Windows).
            msz.__exit__(None, None, None)
