"""
PSM (Peptide-Spectrum Match) data structure.

This module defines the core data structure for representing
peptide identification results.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


@dataclass
class PSM:
    """
    Peptide-Spectrum Match (PSM) data structure.

    Represents a single identification result linking a spectrum to a peptide.

    Attributes:
        scan_number: Scan number from the mass spectrometer.
        peptide: Identified peptide sequence (without modifications).
        charge: Precursor charge state.
        score: Primary search/scoring metric.
        proteins: List of protein accessions.
        spectrum_index: Zero-based index of the spectrum (if known).
        q_value: FDR-controlled q-value.
        pep: Posterior Error Probability.
        retention_time: Retention time in seconds.
        precursor_mz: Precursor m/z value.
        precursor_mass: Precursor neutral mass.
        mass_diff: Difference between observed and calculated mass.
        modified_peptide: Peptide sequence with modification annotations.
        num_matched_ions: Number of matched fragment ions.
        num_total_ions: Total number of theoretical fragment ions.
        rank: Hit rank (1 = best).
        is_decoy: Whether this is a decoy hit.
        extra: Additional format-specific fields.
    """

    scan_number: int
    peptide: str
    charge: int
    score: float
    proteins: List[str] = field(default_factory=list)

    # Optional fields
    spectrum_index: Optional[int] = None
    q_value: Optional[float] = None
    pep: Optional[float] = None
    retention_time: Optional[float] = None
    precursor_mz: Optional[float] = None
    precursor_mass: Optional[float] = None
    mass_diff: Optional[float] = None
    modified_peptide: Optional[str] = None
    num_matched_ions: Optional[int] = None
    num_total_ions: Optional[int] = None
    rank: int = 1
    is_decoy: bool = False

    # Raw data for format-specific fields
    extra: Dict[str, Any] = field(default_factory=dict)

    def __repr__(self) -> str:
        return (
            f"PSM(scan={self.scan_number}, peptide='{self.peptide}', "
            f"charge={self.charge}, score={self.score:.4f})"
        )

    @property
    def is_target(self) -> bool:
        """Whether this is a target (non-decoy) hit."""
        return not self.is_decoy

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
class SearchInfo:
    """
    Search-level metadata carried alongside a set of PSMs.

    Populated by the readers where the source format records it, and written
    into the mzTab ``MTD`` section.

    Attributes:
        software: Name of the search engine / rescoring tool.
        software_version: Version string of that software.
        primary_score_name: Name of the score stored in ``PSM.score``.
        fixed_mods: Fixed modifications searched.
        variable_mods: Variable modifications searched.
    """

    software: Optional[str] = None
    software_version: Optional[str] = None
    primary_score_name: Optional[str] = None
    fixed_mods: List[ModificationSpec] = field(default_factory=list)
    variable_mods: List[ModificationSpec] = field(default_factory=list)
