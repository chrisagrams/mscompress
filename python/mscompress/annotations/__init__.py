"""Search results (peptide-spectrum matches).

Every supported input (pepXML, Percolator .tsv/.pin, mzTab) is read into a
:class:`PSMTable`: a pandas DataFrame with a fixed set of core columns plus
the :class:`SearchMetadata` of the search. MSZX archives store PSM tables as
mzTab 1.0.

Example:
    ```python
    from mscompress.annotations import read_psms

    table = read_psms("results.pepXML")
    confident = table.df[table.df.q_value < 0.01]
    table.to_mztab("results.mztab")
    ```
"""

from __future__ import annotations

from mscompress.annotations.detect import detect_format, read_psms
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.mztab import mztab_member_name, read_mztab, write_mztab
from mscompress.annotations.pepxml import read_pepxml
from mscompress.annotations.percolator import read_percolator
from mscompress.annotations.table import PSM_COLUMNS, PSMTable

__all__ = [
    "PSMTable",
    "PSM_COLUMNS",
    "SearchMetadata",
    "ModificationSpec",
    "read_psms",
    "read_pepxml",
    "read_percolator",
    "read_mztab",
    "write_mztab",
    "mztab_member_name",
    "detect_format",
]
