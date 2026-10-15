"""
Modification helpers shared by the readers and the mzTab writer.

The readers turn each engine's own notation into ``(position, massdiff,
site)`` tuples; :func:`format_modifications` renders them in mzTab notation
(``"1-UNIMOD:35,5-UNIMOD:4"``). Positions are 1-based residues, with 0 for
the N-terminus and ``len(peptide) + 1`` for the C-terminus.
"""

from __future__ import annotations

from typing import Dict, Iterable, List, Optional, Tuple

from mscompress.annotations.metadata import ModificationSpec

# (position, massdiff, site)
Mod = Tuple[int, float, str]

# Common UNIMOD entries: (record id, title, monoisotopic delta). mzTab
# requires a UNIMOD accession over CHEMMOD when one exists.
UNIMOD: List[Tuple[int, str, float]] = [
    (1, "Acetyl", 42.010565),
    (4, "Carbamidomethyl", 57.021464),
    (5, "Carbamyl", 43.005814),
    (7, "Deamidated", 0.984016),
    (21, "Phospho", 79.966331),
    (24, "Propionamide", 71.037114),
    (34, "Methyl", 14.01565),
    (35, "Oxidation", 15.994915),
    (36, "Dimethyl", 28.0313),
    (108, "Nethylmaleimide", 125.047679),
    (121, "GG", 114.042927),
    (188, "Label:13C(6)", 6.020129),
    (214, "iTRAQ4plex", 144.102063),
    (259, "Label:13C(6)15N(2)", 8.014199),
    (267, "Label:13C(6)15N(4)", 10.008269),
    (730, "iTRAQ8plex", 304.20536),
    (737, "TMT6plex", 229.162932),
    (2016, "TMTpro", 304.207146),
]
UNIMOD_MASSES: Dict[str, float] = {f"UNIMOD:{rid}": mass for rid, _, mass in UNIMOD}
# iTRAQ8plex and TMTpro are 0.0018 Da apart.
UNIMOD_TOLERANCE = 0.001

RESIDUE_MASSES: Dict[str, float] = {
    "G": 57.021464, "A": 71.037114, "S": 87.032028, "P": 97.052764,
    "V": 99.068414, "T": 101.047679, "C": 103.009185, "L": 113.084064,
    "I": 113.084064, "N": 114.042927, "D": 115.026943, "Q": 128.058578,
    "K": 128.094963, "E": 129.042593, "M": 131.040485, "H": 137.058912,
    "F": 147.068414, "U": 150.953636, "R": 156.101111, "Y": 163.06332,
    "W": 186.079313, "O": 237.147727,
}
N_TERM_MASS = 1.007825  # H
C_TERM_MASS = 17.00274  # OH
PROTON_MASS = 1.007276


def unimod(massdiff: float) -> Optional[Tuple[int, str]]:
    """Closest UNIMOD (record id, title) for a mass shift."""
    best: Optional[Tuple[int, str]] = None
    best_err = UNIMOD_TOLERANCE
    for rid, title, mass in UNIMOD:
        err = abs(mass - massdiff)
        if err <= best_err:
            best, best_err = (rid, title), err
    return best


def chemmod(massdiff: float) -> str:
    """``CHEMMOD:+15.9949`` style accession for an unknown mass shift."""
    return f"CHEMMOD:{'-' if massdiff < 0 else '+'}{abs(massdiff)!r}"


def accession(massdiff: float) -> str:
    """UNIMOD accession for a mass shift, or a CHEMMOD fallback."""
    hit = unimod(massdiff)
    return f"UNIMOD:{hit[0]}" if hit else chemmod(massdiff)


def with_fixed_mods(mods: List[Mod], peptide: str,
                    fixed_mods: Iterable[ModificationSpec]) -> List[Mod]:
    """Add declared fixed modifications that a modified sequence left out."""
    out = list(mods)
    for spec in fixed_mods:
        if spec.site == "N-term":
            positions = [0] if spec.position.startswith("Any") else []
        elif spec.site == "C-term":
            positions = [len(peptide) + 1] if spec.position.startswith("Any") else []
        else:
            positions = [i + 1 for i, aa in enumerate(peptide) if aa == spec.site]
        for pos in positions:
            if not any(p == pos and abs(d - spec.massdiff) < 0.01 for p, d, _ in out):
                out.append((pos, spec.massdiff, spec.site))
    return out


def format_modifications(mods: Iterable[Mod]) -> Optional[str]:
    """mzTab ``modifications`` cell, or None when unmodified."""
    ordered = sorted(mods, key=lambda m: m[0])
    if not ordered:
        return None
    return ",".join(f"{pos}-{accession(delta)}" for pos, delta, _ in ordered)


def observed_mods(mods: Iterable[Mod]) -> List[ModificationSpec]:
    """Distinct (site, mass) pairs, for sources that don't declare their mods."""
    seen: Dict[Tuple[str, float], ModificationSpec] = {}
    for pos, delta, site in mods:
        position = {"N-term": "Any N-term", "C-term": "Any C-term"}.get(site, "Anywhere")
        seen.setdefault((site, round(delta, 4)), ModificationSpec(delta, site, position))
    return list(seen.values())
