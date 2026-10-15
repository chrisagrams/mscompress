"""
Modification helpers shared by the readers and the mzTab writer.

The readers turn each engine's own notation into ``(position, massdiff,
site)`` tuples; :func:`format_modifications` renders them in mzTab notation
(``"1-UNIMOD:35,5-UNIMOD:4"``). Positions are 1-based residues, with 0 for
the N-terminus and ``len(peptide) + 1`` for the C-terminus.
"""

from __future__ import annotations

from typing import Dict, Iterable, List, Optional, Set, Tuple

from mscompress.annotations.metadata import ModificationSpec

# (position, massdiff, site)
Mod = Tuple[int, float, str]

# Common UNIMOD entries: (record id, title, monoisotopic delta, sites or None
# for any). mzTab requires a UNIMOD accession over CHEMMOD when one exists.
UNIMOD: List[Tuple[int, str, float, Optional[Set[str]]]] = [
    (1, "Acetyl", 42.010565, None),
    (4, "Carbamidomethyl", 57.021464, None),
    (5, "Carbamyl", 43.005814, None),
    (7, "Deamidated", 0.984016, None),
    (21, "Phospho", 79.966331, None),
    (24, "Propionamide", 71.037114, None),
    (27, "Glu->pyro-Glu", -18.010565, {"E"}),
    (28, "Gln->pyro-Glu", -17.026549, {"Q"}),
    (34, "Methyl", 14.01565, None),
    (35, "Oxidation", 15.994915, None),
    (36, "Dimethyl", 28.0313, None),
    (108, "Nethylmaleimide", 125.047679, None),
    (121, "GG", 114.042927, None),
    (188, "Label:13C(6)", 6.020129, None),
    (214, "iTRAQ4plex", 144.102063, None),
    (259, "Label:13C(6)15N(2)", 8.014199, None),
    (267, "Label:13C(6)15N(4)", 10.008269, None),
    (730, "iTRAQ8plex", 304.20536, None),
    (737, "TMT6plex", 229.162932, None),
    (2016, "TMTpro", 304.207146, None),
]
UNIMOD_MASSES: Dict[str, float] = {f"UNIMOD:{rid}": mass for rid, _, mass, _ in UNIMOD}
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


def unimod(massdiff: float, site: str) -> Optional[Tuple[int, str]]:
    """Closest UNIMOD (record id, title) for a mass shift at a site."""
    best: Optional[Tuple[int, str]] = None
    best_err = UNIMOD_TOLERANCE
    for rid, title, mass, sites in UNIMOD:
        err = abs(mass - massdiff)
        if err <= best_err and (sites is None or site in sites):
            best, best_err = (rid, title), err
    return best


def chemmod(massdiff: float) -> str:
    """``CHEMMOD:+15.9949`` style accession for an unknown mass shift."""
    return f"CHEMMOD:{'-' if massdiff < 0 else '+'}{abs(massdiff)!r}"


def accession(massdiff: float, site: str) -> str:
    """UNIMOD accession for a mass shift, or a CHEMMOD fallback."""
    hit = unimod(massdiff, site)
    return f"UNIMOD:{hit[0]}" if hit else chemmod(massdiff)


def snap(massdiff: float, site: str, specs: Iterable[ModificationSpec],
         tolerance: float = 0.01) -> float:
    """The declared mass of a matching modification, else ``massdiff``."""
    best, best_err = massdiff, tolerance
    for spec in specs:
        err = abs(spec.massdiff - massdiff)
        if spec.site == site and err <= best_err:
            best, best_err = spec.massdiff, err
    return best


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
    return ",".join(f"{pos}-{accession(delta, site)}" for pos, delta, site in ordered)


def observed_mods(mods: Iterable[Mod]) -> List[ModificationSpec]:
    """Distinct (site, mass) pairs, for sources that don't declare their mods."""
    seen: Dict[Tuple[str, float], ModificationSpec] = {}
    for pos, delta, site in mods:
        position = {"N-term": "Any N-term", "C-term": "Any C-term"}.get(site, "Anywhere")
        seen.setdefault((site, round(delta, 4)), ModificationSpec(delta, site, position))
    return list(seen.values())
