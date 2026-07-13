"""parquet -> .msz conversion.

Thin orchestrator over `mzml._synthesize_mzml` + `MZMLFile.compress`. The
heavy lifting (schema resolution, mzML synthesis) lives in `mzml.py`.
"""

from __future__ import annotations

import os
import tempfile
from os import PathLike
from pathlib import Path
from typing import Union

from mscompress._core import MSZFile, MZMLFile

from mscompress.parquet.mzml import _synthesize_mzml


def parquet_to_msz(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    use_zlib_binary: bool = False,
    ret_time_unit: str = "minute",
) -> MSZFile:
    """Convert a peptide-level parquet file into a `.msz` file.

    Args:
        parquet_path: Source parquet. Must contain `mz` and `intensity`
            list-typed columns (aliases: `m/z`, `MZ`, `mass_to_charge` for mz;
            `int`, `intensities`, `Intensity`, `INTENSITY` for intensity).
            Optional columns: `precursor`, `charge`, `ret_time`, `peptide`,
            and the combined `peptide_charge` ("PEPTIDE_2") string column.
            Missing optional columns are filled with defaults
            (`ret_time=-1.0`, `precursor=0.0`, `charge=0`).
        output_path: Destination `.msz` path.
        use_zlib_binary: If True, deflate each binary array before base64
            encoding (`MS:1000574`). Default False (`MS:1000576`).
        ret_time_unit: Unit of the parquet `ret_time` column. `"minute"`
            (default) maps to UO:0000031; `"second"` to UO:0000010.

    Returns:
        An open MSZFile for the written output.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))

    with tempfile.TemporaryDirectory(prefix="mscompress-parquet-") as td:
        tmp_mzml = Path(td) / "synthesized.mzML"
        _synthesize_mzml(
            parquet_path,
            tmp_mzml,
            ret_time_unit=ret_time_unit,
            use_zlib_binary=use_zlib_binary,
        )
        with MZMLFile(str(tmp_mzml).encode("utf-8")) as mzml:
            return mzml.compress(str(output_path))
