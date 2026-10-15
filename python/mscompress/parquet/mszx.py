"""parquet -> .mszx + parquet -> annotations TSV.

`parquet_to_mszx` bundles the .msz produced by `parquet_to_msz` with the
per-row annotations into a single archive; the annotations are stored as
mzTab, like every other source of search results. `parquet_to_annotations_tsv`
writes the same annotations as a tab-separated file.
"""

from __future__ import annotations

import os
import tempfile
from os import PathLike
from pathlib import Path
from typing import List, Optional, Tuple, Union

import numpy as np
import pandas as pd
import pyarrow as pa
import pyarrow.parquet as pq

from mscompress.annotations import PSMTable, SearchMetadata, psm_frame
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


def _annotations_frame(parquet_path: Path, *, score_column: Optional[str]) -> pd.DataFrame:
    """One row per parquet row: scan (1-based row number), peptide, charge,
    score, precursor, ret_time and any optional float columns.

    `score_column=None` auto-picks a score column, else leaves `score` empty.
    An explicit name that is not in the schema raises (via `_resolve_schema`).
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

    # Optional float columns we still surface as extras when present (and
    # not already used as the score column).
    extra_cols = [
        c for c in _OPTIONAL_FLOAT_COLUMNS
        if c in schema.names and c != cmap.score
    ]
    read_cols = [c for c in (
        cmap.peptide, cmap.charge, cmap.peptide_charge,
        cmap.precursor, cmap.ret_time, cmap.score,
    ) if c is not None] + extra_cols

    frames = []
    for batch in pf.iter_batches(columns=read_cols):
        peptides, charges = _resolve_peptide_charge(cmap, batch)
        frame = pd.DataFrame({
            "peptide": [_tsv_escape(p) for p in peptides],
            "charge": np.asarray(charges, dtype=np.int64),
            "score": (batch.column(cmap.score).to_numpy(zero_copy_only=False).astype(float)
                      if cmap.score is not None else np.nan),
            "precursor": np.asarray(
                _col_or_default(batch, cmap.precursor, _DEFAULT_PRECURSOR), dtype=float),
            "ret_time": np.asarray(
                _col_or_default(batch, cmap.ret_time, _DEFAULT_RET_TIME), dtype=float),
        })
        for c in extra_cols:
            frame[c] = batch.column(c).to_numpy(zero_copy_only=False).astype(float)
        frames.append(frame)
    df = pd.concat(frames, ignore_index=True) if frames else pd.DataFrame(
        columns=["peptide", "charge", "score", "precursor", "ret_time", *extra_cols])
    df.insert(0, "scan", np.arange(1, len(df) + 1))
    return df


def _write_annotations_tsv(
    parquet_path: Path,
    tsv_path: Path,
    *,
    score_column: Optional[str],
) -> int:
    """Write the annotations as a tab-separated file. Returns the row count.

    `score_column=None` with no auto-resolvable score writes empty `score` cells.
    """
    df = _annotations_frame(parquet_path, score_column=score_column)
    df.rename(columns={"scan": "ScanNr", "peptide": "Peptide", "charge": "Charge"}).to_csv(
        tsv_path, sep="\t", index=False, na_rep="", lineterminator="\n")
    return len(df)


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
      - `<source>.mztab.zst` - annotations (peptide/charge/score…) as mzTab
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

        msz = parquet_to_msz(
            parquet_path, tmp_msz,
            use_zlib_binary=use_zlib_binary,
            ret_time_unit=ret_time_unit,
        )
        try:
            builder = MSZXBuilder(msz, source_name=parquet_path.name)
            if not is_long:
                df = _annotations_frame(parquet_path, score_column=score_column)
                md = SearchMetadata(
                    primary_score="score" if df["score"].notna().any() else None,
                    source_format="parquet",
                    source_file=parquet_path.name,
                )
                builder.add_psms(
                    PSMTable(psm_frame(df), md),
                    name=parquet_path.stem,
                    description=description or f"Annotations extracted from {parquet_path.name}",
                )
            if description:
                builder.set_description(description)
            return builder.save(output_path)
        finally:
            # Release mmap before TemporaryDirectory cleans up tmp_msz (Windows).
            msz.__exit__(None, None, None)
