"""
Controlled-vocabulary parameters used in mzTab ``MTD`` lines.

A parameter is ``[cv label, accession, name, value]``; user parameters leave
the label and accession empty.
"""

from __future__ import annotations

import re
from typing import Dict, List, Optional, Tuple

from mscompress.annotations import mods
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata

Param = Tuple[str, str, str, str]

# Search engines with their own PSI-MS term, keyed by _software_key(name).
_SOFTWARE_TERMS: Dict[str, Tuple[str, str]] = {
    "percolator": ("MS:1001490", "Percolator"),
    "xtandem": ("MS:1001476", "X!Tandem"),
    "msfragger": ("MS:1003014", "MSFragger"),
    "comet": ("MS:1002251", "Comet"),
    "mascot": ("MS:1001207", "Mascot"),
    "sequest": ("MS:1001208", "SEQUEST"),
    "msgf": ("MS:1002048", "MS-GF+"),
    "myrimatch": ("MS:1001585", "MyriMatch"),
    "omssa": ("MS:1001475", "OMSSA"),
    "sage": ("MS:1004007", "Sage"),
    "maxquant": ("MS:1001583", "MaxQuant"),
    "andromeda": ("MS:1002337", "Andromeda"),
    "peptideprophet": ("MS:1002287", "PeptideProphet"),
    "byonic": ("MS:1002261", "Byonic"),
    "metamorpheus": ("MS:1002826", "MetaMorpheus"),
    "pfind": ("MS:1003011", "pFind"),
    "prolucid": ("MS:1002596", "ProLuCID"),
    "tide": ("MS:1002575", "Tide"),
}
_SOFTWARE_BY_ACCESSION = {acc: name for acc, name in _SOFTWARE_TERMS.values()}
GENERIC_SOFTWARE = "MS:1001456"

# Primary score name (lowercase) -> parameter.
_SCORE_PARAMS: Dict[str, Param] = {
    "hyperscore": ("MS", "MS:1001331", "X!Tandem:hyperscore", ""),
    "expect": ("MS", "MS:1001330", "X!Tandem:expect", ""),
    "xcorr": ("MS", "MS:1001155", "SEQUEST:xcorr", ""),
}
_GENERIC_SCORE: Param = ("MS", "MS:1001153", "search engine specific score", "")
_SCORE_BY_ACCESSION: Dict[str, Optional[str]] = {
    "MS:1001331": "hyperscore",
    "MS:1001330": "expect",
    "MS:1001155": "xcorr",
    "MS:1001492": "score",
    "MS:1001153": None,
}
_Q_VALUE_ACCESSIONS = {"MS:1001491", "MS:1002354", "MS:1001364"}
_PEP_ACCESSIONS = {"MS:1001493"}

NO_FIXED_MODS: Param = ("MS", "MS:1002453", "No fixed modifications searched", "")
NO_VARIABLE_MODS: Param = ("MS", "MS:1002454", "No variable modifications searched", "")


def clean(value: str) -> str:
    """Replace characters that would break a TSV cell."""
    return re.sub(r"[\t\r\n]", " ", value)


def format_param(param: Param) -> str:
    return "[" + ", ".join(clean(part) for part in param) + "]"


def parse_param(text: str) -> Optional[Param]:
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        return None
    parts = [p.strip() for p in text[1:-1].split(",", 3)]
    return (parts[0], parts[1], parts[2], parts[3]) if len(parts) == 4 else None


def _software_key(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def software_param(md: SearchMetadata) -> Param:
    """Software as its PSI-MS term, with the version in the value slot."""
    version = md.software_version or ""
    if not md.software:
        return ("MS", GENERIC_SOFTWARE, "analysis software", version)
    term = _SOFTWARE_TERMS.get(_software_key(md.software))
    if term is None:
        return ("", "", md.software, version)
    return ("MS", term[0], term[1], version)


def software_from_param(param: Param) -> Tuple[Optional[str], Optional[str]]:
    """(software, version) from a ``software[1]`` parameter."""
    if param[1] == GENERIC_SOFTWARE:
        software = None
    else:
        software = _SOFTWARE_BY_ACCESSION.get(param[1], param[2] or None)
    return software, param[3] or None


def score_params(md: SearchMetadata) -> List[Param]:
    """Parameters for the primary score, q-value and PEP, in that order."""
    percolator = _software_key(md.software or "") == "percolator"
    name = (md.primary_score or "").lower()
    if not name:
        primary = _GENERIC_SCORE
    elif name == "score" and percolator:
        primary = ("MS", "MS:1001492", "percolator:score", "")
    else:
        primary = _SCORE_PARAMS.get(name, ("", "", md.primary_score or "", ""))
    if percolator:
        return [primary, ("MS", "MS:1001491", "percolator:Q value", ""),
                ("MS", "MS:1001493", "percolator:PEP", "")]
    return [primary, ("MS", "MS:1002354", "PSM-level q-value", ""),
            ("", "", "posterior error probability", "")]


def score_role(param: Param) -> str:
    """Which value a score parameter holds: ``q_value``, ``pep`` or ``score``."""
    _, accession, name, _ = param
    lname = name.lower()
    if accession in _Q_VALUE_ACCESSIONS or "q-value" in lname or "q value" in lname:
        return "q_value"
    if accession in _PEP_ACCESSIONS or "posterior error" in lname or lname == "pep":
        return "pep"
    return "score"


def score_name(param: Param) -> Optional[str]:
    """Primary score name for a score parameter."""
    return _SCORE_BY_ACCESSION.get(param[1], param[2] or None)


def mod_param(spec: ModificationSpec) -> Param:
    hit = mods.unimod(spec.massdiff)
    if hit:
        return ("UNIMOD", f"UNIMOD:{hit[0]}", hit[1], "")
    accession = mods.chemmod(spec.massdiff)
    # The name repeats the accession: jmzTab rejects an empty name.
    return ("CHEMMOD", accession, accession, "")


def mod_mass(param: Param) -> Optional[float]:
    """Mass shift of a UNIMOD / CHEMMOD parameter, if known."""
    if param[1].startswith("CHEMMOD:"):
        try:
            return float(param[1][len("CHEMMOD:"):])
        except ValueError:
            return None
    return mods.UNIMOD_MASSES.get(param[1])
