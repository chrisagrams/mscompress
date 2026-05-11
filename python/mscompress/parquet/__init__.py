"""Parquet ↔ MSZ / MSZX / mzML converters.

Public API:
    parquet_to_msz                  - parquet -> .msz
    parquet_to_annotations_tsv      - parquet -> .tsv
    parquet_to_mszx                 - parquet -> .mszx (msz + tsv bundle)
    to_parquet                      - mzML / .msz / .mszx -> .parquet

Requires the `[parquet]` extra (`pyarrow`).
"""

from __future__ import annotations

import math
import os
from os import PathLike
from pathlib import Path
from typing import Literal, Union

try:
    import pyarrow as pa
    import pyarrow.parquet as pq
except ImportError as e:
    raise ImportError(
        "mscompress.parquet requires pyarrow. Install with: "
        "pip install 'mscompress[parquet]'"
    ) from e

import numpy as np

from mscompress._core import BaseFile
from mscompress.mszx import MSZXFile
from mscompress.utils import read as _read_file

from mscompress.parquet.msz import parquet_to_msz
from mscompress.parquet.mszx import parquet_to_annotations_tsv, parquet_to_mszx
from mscompress.parquet.mzml import _detect_ret_time_unit, _extract_precursor_charge


_MultiPSM = Literal["best", "first", "all"]


def _build_parquet_schema(has_annotations: bool) -> pa.Schema:
    fields = [
        pa.field("scan", pa.int32()),
        pa.field("ms_level", pa.int16()),
        pa.field("ret_time", pa.float32()),
        pa.field("precursor", pa.float32()),
        pa.field("charge", pa.int32()),
        pa.field("mz", pa.list_(pa.float32())),
        pa.field("intensity", pa.list_(pa.float32())),
        pa.field("n_peaks", pa.int32()),
    ]
    if has_annotations:
        fields.extend([
            pa.field("peptide", pa.string()),
            pa.field("peptide_charge", pa.string()),
            pa.field("score", pa.float32()),
            pa.field("q_value", pa.float32()),
        ])
    return pa.schema(fields)


def _select_psms(psms, mode: _MultiPSM):
    """Apply multi_psm policy. Returns a list with at least one entry; a single
    None means "no PSM for this scan -> emit null annotation cells"."""
    if not psms:
        return [None]
    if mode == "best":
        return [max(psms, key=lambda p: p.score)]
    if mode == "first":
        return [psms[0]]
    if mode == "all":
        return list(psms)
    raise ValueError(f"multi_psm must be 'best', 'first', or 'all'; got {mode!r}")


def _flush_batch(writer: pq.ParquetWriter, schema: pa.Schema, cols: dict) -> None:
    arrays = []
    for field in schema:
        name = field.name
        if name in ("mz", "intensity"):
            # Cast each spectrum's numpy array to float32 before handing
            # pyarrow a list of arrays; otherwise pyarrow infers float64 and
            # bloats the file (and breaks bit-exact mz/intensity round-trip
            # with the forward path, which expects float32).
            arrays.append(pa.array(
                [a.astype(np.float32, copy=False) for a in cols[name]],
                type=field.type,
            ))
        else:
            arrays.append(pa.array(cols[name], type=field.type))
    writer.write_batch(pa.record_batch(arrays, schema=schema))
    for v in cols.values():
        v.clear()


def to_parquet(
    input: Union[str, PathLike, bytes, BaseFile, MSZXFile],
    output_path: Union[str, PathLike],
    *,
    compression: str = "zstd",
    row_group_size: int = 1024,
    multi_psm: _MultiPSM = "best",
) -> Path:
    """Convert an mzML, MSZ, or MSZX file into a spectrum-level parquet.

    The output schema is designed to round-trip cleanly through
    `parquet_to_msz` / `parquet_to_mszx`: column names and dtypes match what
    those functions consume, including a `peptide_charge` column built from
    annotation peptide + charge so the forward path can re-split it.

    Retention time unit is auto-detected from the first spectrum's
    `MS:1000016` cvParam — no caller parameter. MSZX annotations (peptide /
    charge / score / q_value) are included when present.

    Args:
        input: A path to an mzML / `.msz` / `.mszx` file, or an already-open
            `MZMLFile` / `MSZFile` / `MSZXFile`.
        output_path: Destination `.parquet` path.
        compression: Parquet compression codec. Default `"zstd"`.
        row_group_size: Spectra per parquet row group. Default 1024.
        multi_psm: How to handle scans with multiple PSMs (MSZX only).
            `"best"` (default) keeps the max-score PSM, `"first"` keeps the
            first, `"all"` emits one row per PSM (duplicating the spectrum
            payload). Ignored when input has no annotations.

    Returns:
        Path to the written parquet file.
    """
    output_path = Path(os.fspath(output_path))

    owns_file = False
    if isinstance(input, (str, bytes, PathLike)):
        f = _read_file(input)
        owns_file = True
    else:
        f = input

    try:
        psm_reader = f.annotations if isinstance(f, MSZXFile) else None
        has_annotations = psm_reader is not None
        rt_unit = _detect_ret_time_unit(f)
        rt_scale = (1.0 / 60.0) if rt_unit == "minute" else 1.0
        schema = _build_parquet_schema(has_annotations)

        col_names = [field.name for field in schema]
        cols: dict = {name: [] for name in col_names}
        n_buffered = 0

        with pq.ParquetWriter(str(output_path), schema, compression=compression) as writer:
            for spectrum in f.spectra:
                rt_s = spectrum.retention_time
                rt_out = rt_s * rt_scale if not (isinstance(rt_s, float) and math.isnan(rt_s)) else rt_s
                precursor, charge = _extract_precursor_charge(spectrum.xml)
                mz_arr = np.asarray(spectrum.mz, dtype=np.float32)
                in_arr = np.asarray(spectrum.intensity, dtype=np.float32)
                n_peaks = int(len(mz_arr))

                if psm_reader is not None:
                    rows = _select_psms(psm_reader.get_by_scan(spectrum.scan), multi_psm)
                else:
                    rows = [None]

                for psm in rows:
                    cols["scan"].append(int(spectrum.scan))
                    cols["ms_level"].append(int(spectrum.ms_level))
                    cols["ret_time"].append(float(rt_out))
                    cols["precursor"].append(float(precursor))
                    cols["charge"].append(int(charge))
                    cols["mz"].append(mz_arr)
                    cols["intensity"].append(in_arr)
                    cols["n_peaks"].append(n_peaks)
                    if has_annotations:
                        if psm is None:
                            cols["peptide"].append(None)
                            cols["peptide_charge"].append(None)
                            cols["score"].append(None)
                            cols["q_value"].append(None)
                        else:
                            cols["peptide"].append(psm.peptide)
                            cols["peptide_charge"].append(f"{psm.peptide}_{psm.charge}")
                            cols["score"].append(float(psm.score))
                            cols["q_value"].append(
                                float(psm.q_value) if psm.q_value is not None else None
                            )
                    n_buffered += 1
                    if n_buffered >= row_group_size:
                        _flush_batch(writer, schema, cols)
                        n_buffered = 0

            if n_buffered > 0:
                _flush_batch(writer, schema, cols)

        return output_path
    finally:
        if owns_file:
            try:
                f.__exit__(None, None, None)
            except Exception:
                pass


__all__ = [
    "parquet_to_msz",
    "parquet_to_annotations_tsv",
    "parquet_to_mszx",
    "to_parquet",
]
