"""pepXML: search engine output (Comet, MSFragger, X! Tandem, ...)."""

from __future__ import annotations

import io
import re

import pandas as pd
from typing import Any, Dict, List, Optional, Tuple
from xml.etree import ElementTree as ET

from mscompress.annotations import mods
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat

# Preference order for the primary score; otherwise the first one listed.
_PRIMARY_SCORES = ("hyperscore", "xcorr", "expect")
_ION_COUNTS = ["num_matched_ions", "num_total_ions"]


def _tag(elem: ET.Element) -> str:
    return elem.tag.rsplit("}", 1)[-1]


class PepXML(SearchResultsFormat):
    """
    pepXML reader.

    Besides the core columns, the table has ``rank``, ``modified_peptide``,
    ``precursor_mass``, ``mass_diff``, ``num_matched_ions``,
    ``num_total_ions`` and one column per ``search_score``.
    """

    format = AnnotationFormat.PEPXML
    extensions = (".pepxml", ".pep.xml")

    def __init__(self, max_rank: int = 1, decoy_prefix: Optional[str] = None):
        """
        Args:
            max_rank: Keep hits with ``hit_rank <= max_rank``.
            decoy_prefix: Protein prefix marking decoys. Defaults to the
                file's ``decoy_prefix`` search parameter, else ``"DECOY_"``.
        """
        self.max_rank = max_rank
        self.decoy_prefix = decoy_prefix

    @classmethod
    def sniff(cls, head: str) -> bool:
        return re.search(r"<(\w+:)?(msms_pipeline_analysis|spectrum_query)\b", head) is not None

    def _parse(self, data: bytes) -> PSMTable:
        md = SearchMetadata(source_format=self.format.value)
        decoy_prefix = self.decoy_prefix or "DECOY_"
        have_summary = False
        rows: List[Dict[str, Any]] = []
        score_names: List[str] = []

        for _, elem in ET.iterparse(io.BytesIO(data), events=("end",)):
            tag = _tag(elem)
            if tag == "search_summary" and not have_summary:
                md, file_decoy_prefix = self._search_summary(elem)
                decoy_prefix = self.decoy_prefix or file_decoy_prefix or decoy_prefix
                have_summary = True
            elif tag == "spectrum_query":
                rows.extend(self._spectrum_query(elem, md, decoy_prefix, score_names))
                elem.clear()

        df = pd.DataFrame(rows)
        if rows:
            # Optional attributes: strings, or None where the file omits them.
            df[_ION_COUNTS] = df[_ION_COUNTS].apply(pd.to_numeric)
        if score_names:
            md.primary_score = next((s for s in _PRIMARY_SCORES if s in score_names),
                                    score_names[0])
            df["score"] = df[md.primary_score]
        return PSMTable(df, md)

    def _search_summary(self, elem: ET.Element) -> Tuple[SearchMetadata, Optional[str]]:
        """Search engine, modifications and decoy prefix."""
        md = SearchMetadata(
            software=elem.get("search_engine") or None,
            software_version=elem.get("search_engine_version") or None,
            source_format=self.format.value,
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
            elif tag == "aminoacid_modification":
                spec = ModificationSpec(float(child.get("massdiff")), child.get("aminoacid"))
                (md.variable_mods if child.get("variable") == "Y" else md.fixed_mods).append(spec)
            elif tag == "terminal_modification":
                end = child.get("terminus").upper()
                scope = "Protein" if child.get("protein_terminus") == "Y" else "Any"
                spec = ModificationSpec(float(child.get("massdiff")), f"{end}-term",
                                        f"{scope} {end}-term")
                (md.variable_mods if child.get("variable") == "Y" else md.fixed_mods).append(spec)
        return md, decoy_prefix

    def _spectrum_query(
        self, query: ET.Element, md: SearchMetadata, decoy_prefix: str, score_names: List[str]
    ) -> List[Dict[str, Any]]:
        """One row per search hit of rank <= max_rank."""
        charge = int(query.get("assumed_charge"))
        precursor_mass = float(query.get("precursor_neutral_mass"))

        rows = []
        for hit in query.iter():
            if _tag(hit) != "search_hit" or int(hit.get("hit_rank")) > self.max_rank:
                continue
            peptide = hit.get("peptide")
            proteins = [hit.get("protein")]
            scores: Dict[str, float] = {}
            pep = None
            for child in hit.iter():
                tag = _tag(child)
                if tag == "alternative_protein" and child.get("protein") not in proteins:
                    proteins.append(child.get("protein"))
                elif tag == "search_score":
                    scores[child.get("name")] = float(child.get("value"))
                elif tag == "peptideprophet_result":
                    pep = 1.0 - float(child.get("probability"))
            if not score_names:
                score_names.extend(scores)

            hit_mods, modified = self._hit_mods(hit, peptide, md)
            calc_mass = float(hit.get("calc_neutral_pep_mass"))
            rows.append({
                "scan": int(query.get("start_scan")),
                "peptide": peptide,
                "modifications": mods.format_modifications(hit_mods),
                "charge": charge,
                "pep": pep,
                "retention_time": query.get("retention_time_sec"),
                "precursor_mz": (precursor_mass + charge * mods.PROTON_MASS) / charge,
                "calc_mz": (calc_mass + charge * mods.PROTON_MASS) / charge,
                "proteins": proteins,
                "pre": hit.get("peptide_prev_aa"),
                "post": hit.get("peptide_next_aa"),
                "is_decoy": all(p.startswith(decoy_prefix) for p in proteins),
                "rank": int(hit.get("hit_rank")),
                "modified_peptide": modified or peptide,
                "precursor_mass": precursor_mass,
                "mass_diff": float(hit.get("massdiff")),
                "num_matched_ions": hit.get("num_matched_ions"),
                "num_total_ions": hit.get("tot_num_ions"),
                **scores,
            })
        return rows

    @staticmethod
    def _hit_mods(
        hit: ET.Element, peptide: str, md: SearchMetadata
    ) -> Tuple[List[mods.Mod], Optional[str]]:
        """A hit's modifications, and its modified sequence if given."""
        found: List[mods.Mod] = []
        info = next((e for e in hit if _tag(e) == "modification_info"), None)
        if info is not None:
            if (nterm := info.get("mod_nterm_mass")) is not None:
                found.append((0, float(nterm) - mods.N_TERM_MASS, "N-term"))
            if (cterm := info.get("mod_cterm_mass")) is not None:
                found.append((len(peptide) + 1, float(cterm) - mods.C_TERM_MASS, "C-term"))
            for aa_mod in info:
                if _tag(aa_mod) == "mod_aminoacid_mass":
                    # The modified residue's total mass.
                    pos = int(aa_mod.get("position"))
                    site = peptide[pos - 1]
                    found.append((pos, float(aa_mod.get("mass")) - mods.RESIDUE_MASSES[site], site))
        found = mods.with_fixed_mods(found, peptide, md.fixed_mods)
        return found, info.get("modified_peptide") if info is not None else None
