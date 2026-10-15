"""Search results (peptide-spectrum matches).

Every supported input is read into a :class:`PSMTable`: a pandas DataFrame
with a fixed set of core columns plus the :class:`SearchMetadata` of the
search. Each format is a :class:`SearchResultsFormat` with a ``read()``;
:class:`MzTab`, the format MSZX archives store, also has ``write()``.

Example:
    ```python
    from mscompress.annotations import MzTab, PepXML, read_psms

    table = read_psms("results.pepXML")          # any format, detected
    table = PepXML(max_rank=2).read("results.pepXML")
    confident = table.df[table.df.q_value < 0.01]
    MzTab().write(table, "results.mztab")
    ```
"""

from __future__ import annotations

from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.detect import FORMATS, detect_format, read_psms
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.mztab import MzTab
from mscompress.annotations.pepxml import PepXML
from mscompress.annotations.percolator import Percolator
from mscompress.annotations.table import PSM_COLUMNS, PSMTable

__all__ = [
    "PSMTable",
    "PSM_COLUMNS",
    "SearchMetadata",
    "ModificationSpec",
    "SearchResultsFormat",
    "MzTab",
    "PepXML",
    "Percolator",
    "FORMATS",
    "read_psms",
    "detect_format",
]
