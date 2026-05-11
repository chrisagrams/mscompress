"""Parquet ↔ MSZ / MSZX / mzML converters.

Public API:
    from_parquet  - parquet -> .msz / .mszx / .tsv (dispatched by extension)
    to_parquet    - mzML / .msz / .mszx -> .parquet

The per-output-type forward functions (`parquet_to_msz`, `parquet_to_mszx`,
`parquet_to_annotations_tsv`) remain available on their submodules:
    from mscompress.parquet.msz  import parquet_to_msz
    from mscompress.parquet.mszx import parquet_to_mszx, parquet_to_annotations_tsv

Requires the `[parquet]` extra (`pyarrow`).
"""

from __future__ import annotations

try:
    import pyarrow  # noqa: F401  (gate the package on pyarrow availability)
except ImportError as e:
    raise ImportError(
        "mscompress.parquet requires pyarrow. Install with: "
        "pip install 'mscompress[parquet]'"
    ) from e

from mscompress.parquet.reader import from_parquet
from mscompress.parquet.writer import to_parquet


__all__ = [
    "from_parquet",
    "to_parquet",
]
