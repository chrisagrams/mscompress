"""Forward-path dispatcher: parquet -> .msz / .mszx / .tsv.

Public surface:
    `from_parquet(parquet_path, output_path, ...)` — picks the right
    per-output-type writer based on the destination extension (or an
    explicit `output_type=` override).

The actual conversions live in their respective modules
(`msz.parquet_to_msz`, `mszx.parquet_to_mszx`, `mszx.parquet_to_annotations_tsv`);
this module is just the dispatcher.
"""

from __future__ import annotations

import os
from os import PathLike
from pathlib import Path
from typing import Literal, Optional, Union

from mscompress.parquet.msz import parquet_to_msz as _parquet_to_msz
from mscompress.parquet.mszx import (
    parquet_to_annotations_tsv as _parquet_to_annotations_tsv,
    parquet_to_mszx as _parquet_to_mszx,
)


_OutputType = Literal["msz", "mszx", "tsv"]
_EXT_TO_OUTPUT: dict[str, _OutputType] = {
    ".msz": "msz",
    ".mszx": "mszx",
    ".tsv": "tsv",
}


def _infer_output_type(output_path: Path) -> Optional[_OutputType]:
    """Map the path suffix to one of the supported output types, or None."""
    return _EXT_TO_OUTPUT.get(output_path.suffix.lower())


def from_parquet(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    output_type: Optional[_OutputType] = None,
    use_zlib_binary: bool = False,
    ret_time_unit: str = "minute",
    score_column: Optional[str] = None,
    description: Optional[str] = None,
) -> Path:
    """Convert a parquet into `.msz`, `.mszx`, or an annotations `.tsv`.

    Output type is inferred from `output_path`'s extension by default
    (`.msz`, `.mszx`, `.tsv`, case-insensitive); pass `output_type=` to
    override or when the extension is unknown / missing.

    Args:
        parquet_path: Source parquet.
        output_path: Destination path. Extension determines the format unless
            overridden by `output_type`.
        output_type: Explicit format override. One of `"msz"`, `"mszx"`,
            `"tsv"`.
        use_zlib_binary: zlib-deflate binary arrays inside the synthesized
            mzML before base64 encoding. Applies to `msz`/`mszx` only.
        ret_time_unit: `"minute"` (default) or `"second"`. Applies to
            `msz`/`mszx` only.
        score_column: Parquet column to surface as PSM `score`. Applies to
            `mszx`/`tsv` only. `None` auto-picks `max_score`/`mean_score`/
            `score`/`Score` if present, otherwise leaves the score cells
            empty. An explicit name absent from the schema raises.
        description: Human-readable description embedded in the archive
            manifest. Applies to `mszx` only.

    Returns:
        The written `output_path`.

    Raises:
        ValueError: If the output type can't be inferred and `output_type`
            wasn't provided, or if it isn't one of the supported values.
    """
    output_path = Path(os.fspath(output_path))
    if output_type is None:
        output_type = _infer_output_type(output_path)
        if output_type is None:
            raise ValueError(
                f"Cannot infer output type from suffix {output_path.suffix!r}. "
                f"Pass output_type='msz' | 'mszx' | 'tsv' explicitly."
            )

    if output_type == "msz":
        msz = _parquet_to_msz(
            parquet_path, output_path,
            use_zlib_binary=use_zlib_binary,
            ret_time_unit=ret_time_unit,
        )
        # Close the handle so the caller just gets a Path back, matching the
        # mszx/tsv branches. Callers who want an open file: read(output_path).
        msz.__exit__(None, None, None)
        return output_path
    if output_type == "mszx":
        return _parquet_to_mszx(
            parquet_path, output_path,
            score_column=score_column,
            use_zlib_binary=use_zlib_binary,
            ret_time_unit=ret_time_unit,
            description=description,
        )
    if output_type == "tsv":
        return _parquet_to_annotations_tsv(
            parquet_path, output_path,
            score_column=score_column,
        )
    raise ValueError(
        f"output_type must be 'msz', 'mszx', or 'tsv'; got {output_type!r}"
    )
