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
from typing import Optional, Union

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
    _resolve_schema,
    _split_peptide_charge,
)


def _tsv_escape(value: object) -> str:
    """Render a TSV cell, replacing whitespace that would break the row."""
    if value is None:
        return ""
    s = str(value)
    return s.replace("\t", " ").replace("\n", " ").replace("\r", " ")


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

    # Optional float columns we still surface as TSV extras when present (and
    # not already used as the score column).
    extra_cols = [
        c for c in _OPTIONAL_FLOAT_COLUMNS
        if c in schema.names and c != cmap.score
    ]
    columns = [c for c in (
        cmap.peptide, cmap.charge, cmap.peptide_charge,
        cmap.precursor, cmap.ret_time, cmap.score,
    ) if c is not None] + extra_cols

    headers = ["ScanNr", "Peptide", "Charge", "score", "precursor", "ret_time"] + extra_cols

    n = 0
    with open(tsv_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("\t".join(headers) + "\n")
        for batch in pf.iter_batches(columns=columns):
            peptide_col = (
                batch.column(cmap.peptide).to_pylist() if cmap.peptide else None
            )
            charge_col = (
                batch.column(cmap.charge).to_numpy(zero_copy_only=False)
                if cmap.charge else None
            )
            pep_charge_col = (
                batch.column(cmap.peptide_charge).to_pylist()
                if cmap.peptide_charge else None
            )
            precursor_col = (
                batch.column(cmap.precursor).to_numpy(zero_copy_only=False)
                if cmap.precursor else None
            )
            ret_time_col = (
                batch.column(cmap.ret_time).to_numpy(zero_copy_only=False)
                if cmap.ret_time else None
            )
            score_col = (
                batch.column(cmap.score).to_numpy(zero_copy_only=False)
                if cmap.score else None
            )
            extras = {
                c: batch.column(c).to_numpy(zero_copy_only=False) for c in extra_cols
            }

            for i in range(batch.num_rows):
                n += 1
                if peptide_col is not None:
                    peptide_val = peptide_col[i] or _DEFAULT_PEPTIDE
                    charge_val = (
                        int(charge_col[i]) if charge_col is not None else _DEFAULT_CHARGE
                    )
                elif pep_charge_col is not None:
                    peptide_val, charge_val = _split_peptide_charge(
                        pep_charge_col[i] or ""
                    )
                else:
                    peptide_val = _DEFAULT_PEPTIDE
                    charge_val = (
                        int(charge_col[i]) if charge_col is not None else _DEFAULT_CHARGE
                    )

                precursor_val = (
                    float(precursor_col[i]) if precursor_col is not None
                    else _DEFAULT_PRECURSOR
                )
                ret_time_val = (
                    float(ret_time_col[i]) if ret_time_col is not None
                    else _DEFAULT_RET_TIME
                )
                score_cell = (
                    repr(float(score_col[i])) if score_col is not None else ""
                )

                row = [
                    str(n),
                    _tsv_escape(peptide_val),
                    str(charge_val),
                    score_cell,
                    repr(precursor_val),
                    repr(ret_time_val),
                ]
                for c in extra_cols:
                    row.append(repr(float(extras[c][i])))
                out.write("\t".join(row) + "\n")

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
            _write_annotations_tsv(parquet_path, tmp_tsv, score_column=score_column)
            reader = TSVReader(tmp_tsv)
            builder = MSZXBuilder(msz, source_name=parquet_path.name)
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
