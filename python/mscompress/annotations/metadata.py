"""Search-level metadata carried alongside a PSM table."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


@dataclass
class ModificationSpec:
    """
    A modification searched for, as declared by the search engine.

    Attributes:
        massdiff: Mass shift in Daltons.
        site: Residue letter, or "N-term" / "C-term".
        position: One of "Anywhere", "Any N-term", "Any C-term",
            "Protein N-term", "Protein C-term".
    """

    massdiff: float
    site: str
    position: str = "Anywhere"


@dataclass
class SearchMetadata:
    """
    What produced a set of PSMs. Written to, and read from, the mzTab ``MTD``
    section.

    Attributes:
        software: Search engine / rescoring tool.
        software_version: Version of that software.
        primary_score: Name of the score in the ``score`` column.
        fixed_mods: Fixed modifications searched.
        variable_mods: Variable modifications searched.
        ms_run_location: Spectra file the PSMs refer to.
        source_format: Format the PSMs were read from (``"pepxml"``, ...).
        source_file: File name the PSMs were read from.
    """

    software: Optional[str] = None
    software_version: Optional[str] = None
    primary_score: Optional[str] = None
    fixed_mods: List[ModificationSpec] = field(default_factory=list)
    variable_mods: List[ModificationSpec] = field(default_factory=list)
    ms_run_location: Optional[str] = None
    source_format: Optional[str] = None
    source_file: Optional[str] = None
