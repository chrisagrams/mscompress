"""pepXML reader."""

from __future__ import annotations

import io
import re
from typing import Any, Dict, List, Optional
from xml.etree import ElementTree as ET

from mscompress.annotations import mods
from mscompress.annotations._io import Source, load
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.table import PSMTable

# Preference order for the primary score.
_PRIMARY_SCORES = ("hyperscore", "xcorr", "expect")
_DEFAULT_DECOY_PREFIX = "DECOY_"


def _tag(elem: ET.Element) -> str:
    return elem.tag.rsplit("}", 1)[-1]


def _float(value: Optional[str]) -> Optional[float]:
    try:
        return float(value) if value is not None else None
    except ValueError:
        return None


def _parse_search_summary(elem: ET.Element) -> tuple[SearchMetadata, Optional[str]]:
    """Search engine, modifications and decoy prefix from ``search_summary``."""
    md = SearchMetadata(
        software=elem.get("search_engine") or None,
        software_version=elem.get("search_engine_version") or None,
        source_format="pepxml",
    )
    # MSFragger writes search_engine="X! Tandem" and names itself in the
    # version ("MSFragger-4.0").
    fragger = re.match(r"MSFragger[-_ ]?(.*)$", md.software_version or "", re.I)
    if fragger:
        md.software, md.software_version = "MSFragger", fragger.group(1) or None

    decoy_prefix = None
    for child in elem:
        tag = _tag(child)
        if tag == "parameter" and child.get("name") == "decoy_prefix":
            decoy_prefix = child.get("value") or None
            continue
        massdiff = _float(child.get("massdiff"))
        if massdiff is None:
            continue
        if tag == "aminoacid_modification":
            spec = ModificationSpec(massdiff, child.get("aminoacid", ""), "Anywhere")
        elif tag == "terminal_modification":
            end = "N" if child.get("terminus", "n").upper() == "N" else "C"
            scope = "Protein" if child.get("protein_terminus", "N").upper() == "Y" else "Any"
            spec = ModificationSpec(massdiff, f"{end}-term", f"{scope} {end}-term")
        else:
            continue
        variable = child.get("variable", "N").upper() == "Y"
        (md.variable_mods if variable else md.fixed_mods).append(spec)
    return md, decoy_prefix


def _hit_mods(hit: ET.Element, peptide: str, md: SearchMetadata) -> tuple[List[mods.Mod], Optional[str]]:
    """A hit's modifications, and its modified sequence if given."""
    found: List[mods.Mod] = []
    modified = None
    specs = md.fixed_mods + md.variable_mods
    info = next((e for e in hit if _tag(e) == "modification_info"), None)
    if info is not None:
        modified = info.get("modified_peptide")
        for site, attr, base in (("N-term", "mod_nterm_mass", mods.N_TERM_MASS),
                                 ("C-term", "mod_cterm_mass", mods.C_TERM_MASS)):
            mass = _float(info.get(attr))
            if mass is not None:
                pos = 0 if site == "N-term" else len(peptide) + 1
                found.append((pos, mods.snap(mass - base, site, specs), site))
        for aa_mod in info:
            if _tag(aa_mod) != "mod_aminoacid_mass":
                continue
            pos = int(aa_mod.get("position", "0"))
            if not 1 <= pos <= len(peptide):
                continue
            site = peptide[pos - 1]
            # pepXML 1.2+ gives the shift directly; older files only the
            # modified residue's total mass.
            delta = _float(aa_mod.get("variable")) or _float(aa_mod.get("static"))
            if delta is None:
                mass = _float(aa_mod.get("mass"))
                if mass is None or site not in mods.RESIDUE_MASSES:
                    continue
                delta = mass - mods.RESIDUE_MASSES[site]
            found.append((pos, mods.snap(delta, site, specs), site))
    return mods.with_fixed_mods(found, peptide, md.fixed_mods), modified


def read_pepxml(
    source: Source,
    max_rank: int = 1,
    decoy_prefix: Optional[str] = None,
) -> PSMTable:
    """
    Read a pepXML file.

    Args:
        source: Path to the file, or its contents (zstd is decompressed).
        max_rank: Keep hits with ``hit_rank <= max_rank``.
        decoy_prefix: Protein prefix marking decoys. Defaults to the file's
            ``decoy_prefix`` search parameter, else ``"DECOY_"``.

    Returns:
        The PSMs. Besides the core columns: ``rank``, ``modified_peptide``,
        ``precursor_mass``, ``mass_diff``, ``num_matched_ions``,
        ``num_total_ions`` and one column per ``search_score``.
    """
    data, name = load(source)
    md = SearchMetadata(source_format="pepxml")
    file_decoy_prefix = None
    have_summary = False
    rows: List[Dict[str, Any]] = []

    for _, elem in ET.iterparse(io.BytesIO(data), events=("end",)):
        tag = _tag(elem)
        if tag == "search_summary" and not have_summary:
            md, file_decoy_prefix = _parse_search_summary(elem)
            have_summary = True
        elif tag == "spectrum_query":
            prefix = decoy_prefix or file_decoy_prefix or _DEFAULT_DECOY_PREFIX
            rows.extend(_parse_spectrum_query(elem, md, max_rank, prefix))
            elem.clear()

    md.source_file = name
    if rows:
        md.primary_score = next((s for s in _PRIMARY_SCORES if s in rows[0]), None)
        if md.primary_score is None:
            md.primary_score = rows[0].pop("_first_score", None)
    for row in rows:
        row.pop("_first_score", None)
        row["score"] = row.get(md.primary_score) if md.primary_score else None
    return PSMTable(rows, md)


def _parse_spectrum_query(
    query: ET.Element, md: SearchMetadata, max_rank: int, decoy_prefix: str
) -> List[Dict[str, Any]]:
    scan_match = re.search(r"\.(\d+)\.", query.get("spectrum", ""))
    scan = int(scan_match.group(1)) if scan_match else int(query.get("start_scan", 0))
    charge = int(query.get("assumed_charge", 0)) or None
    precursor_mass = _float(query.get("precursor_neutral_mass"))

    def mz(mass: Optional[float]) -> Optional[float]:
        if mass is None or not charge:
            return None
        return (mass + charge * mods.PROTON_MASS) / charge

    rows = []
    for hit in query.iter():
        if _tag(hit) != "search_hit":
            continue
        rank = int(hit.get("hit_rank", 1))
        if rank > max_rank:
            continue
        peptide = hit.get("peptide", "")
        proteins = [hit.get("protein")] if hit.get("protein") else []
        scores: Dict[str, float] = {}
        pep = None
        for child in hit.iter():
            tag = _tag(child)
            if tag == "alternative_protein" and child.get("protein"):
                if child.get("protein") not in proteins:
                    proteins.append(child.get("protein"))
            elif tag == "search_score":
                value = _float(child.get("value"))
                if value is not None:
                    scores[child.get("name", "")] = value
            elif tag == "peptideprophet_result":
                probability = _float(child.get("probability"))
                if probability is not None:
                    pep = 1.0 - probability

        hit_mods, modified = _hit_mods(hit, peptide, md)
        calc_mass = _float(hit.get("calc_neutral_pep_mass"))
        row: Dict[str, Any] = {
            "scan": scan,
            "peptide": peptide,
            "modifications": mods.format_modifications(hit_mods),
            "charge": charge,
            "pep": pep,
            "retention_time": _float(query.get("retention_time_sec")),
            "precursor_mz": mz(precursor_mass),
            "calc_mz": mz(calc_mass),
            "proteins": proteins,
            "pre": hit.get("peptide_prev_aa") or None,
            "post": hit.get("peptide_next_aa") or None,
            "is_decoy": bool(proteins) and all(p.startswith(decoy_prefix) for p in proteins),
            "rank": rank,
            "modified_peptide": modified or peptide,
            "precursor_mass": precursor_mass,
            "mass_diff": _float(hit.get("massdiff")),
            "num_matched_ions": int(hit.get("num_matched_ions", 0)),
            "num_total_ions": int(hit.get("tot_num_ions", 0)),
            **scores,
        }
        if scores:
            row["_first_score"] = next(iter(scores))
        rows.append(row)
    return rows
