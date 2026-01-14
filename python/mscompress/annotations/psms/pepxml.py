"""
pepXML file reader.

This module provides a reader for pepXML search result files.
"""

from __future__ import annotations

import io
import re
from pathlib import Path
from typing import Dict, List, Optional, Set, Union
from xml.etree import ElementTree as ET

from mscompress.annotations._base import BaseAnnotationFile
from mscompress.annotations.psms._base import BasePSMReader
from mscompress.annotations.psms._types import PSM
from mscompress.types import AnnotationFormat


class PepXMLReader(BasePSMReader):
    """
    Reader for pepXML search result files.

    pepXML is an XML format for peptide identification results from
    database search engines like Comet, X!Tandem, MSFragger, etc.
    
    Supports reading from file paths (compressed or uncompressed),
    tar archive members, or raw bytes via AnnotationSource.

    Example:
        >>> reader = PepXMLReader("results.pepXML")
        >>> for psm in reader:
        ...     print(psm.peptide, psm.score)
        >>>
        >>> # Get PSMs for a specific scan
        >>> psms = reader.get_by_scan(1234)
    """

    # pepXML namespace
    NS = {"pepxml": "http://regis-web.systemsbiology.net/pepXML"}

    def __init__(
        self,
        source: Union[str, Path, BaseAnnotationFile],
        min_rank: int = 1,
        decoy_prefix: str = "DECOY_",
    ):
        """
        Initialize the pepXML reader.

        Args:
            source: Source for reading - file path or BaseAnnotationFile.
            min_rank: Only include hits with rank <= min_rank.
            decoy_prefix: Prefix used to identify decoy proteins.
        """
        super().__init__(source)
        self._min_rank = min_rank
        self._decoy_prefix = decoy_prefix

    @property
    def format(self) -> AnnotationFormat:
        """Return the format identifier."""
        return AnnotationFormat.PEPXML

    def _parse(self) -> None:
        """Parse the pepXML file."""
        # Get decompressed data from source
        data = self._source.read()
        
        # Use iterparse with BytesIO for memory efficiency
        context = ET.iterparse(io.BytesIO(data), events=("end",))

        for event, elem in context:
            # Handle both namespaced and non-namespaced elements
            tag = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag

            if tag == "spectrum_query":
                psms = self._parse_spectrum_query(elem)
                self._psms.extend(psms)
                elem.clear()

    def _parse_spectrum_query(self, elem: ET.Element) -> List[PSM]:
        """Parse a spectrum_query element."""
        psms: List[PSM] = []

        # Get spectrum attributes
        spectrum = elem.get("spectrum", "")
        scan_match = re.search(r"\.(\d+)\.", spectrum)
        scan_number = (
            int(scan_match.group(1)) if scan_match else int(elem.get("start_scan", 0))
        )

        precursor_mass = float(elem.get("precursor_neutral_mass", 0))
        charge = int(elem.get("assumed_charge", 2))
        precursor_mz = precursor_mass / (charge + 1.00727646677)
        retention_time = float(elem.get("retention_time_sec", 0))

        # Find search_result elements
        for search_result in elem.iter():
            sr_tag = (
                search_result.tag.split("}")[-1]
                if "}" in search_result.tag
                else search_result.tag
            )
            if sr_tag != "search_result":
                continue

            for search_hit in search_result.iter():
                sh_tag = (
                    search_hit.tag.split("}")[-1]
                    if "}" in search_hit.tag
                    else search_hit.tag
                )
                if sh_tag != "search_hit":
                    continue

                rank = int(search_hit.get("hit_rank", 1))
                if rank > self._min_rank:
                    continue

                psm = self._parse_search_hit(
                    search_hit,
                    scan_number,
                    charge,
                    precursor_mz,
                    precursor_mass,
                    retention_time,
                    rank,
                )
                if psm:
                    psms.append(psm)

        return psms

    def _parse_search_hit(
        self,
        elem: ET.Element,
        scan_number: int,
        charge: int,
        precursor_mz: float,
        precursor_mass: float,
        retention_time: float,
        rank: int,
    ) -> Optional[PSM]:
        """Parse a search_hit element."""
        try:
            peptide = elem.get("peptide", "")
            protein = elem.get("protein", "")
            proteins = [protein] if protein else []

            # Get additional proteins
            for alt_protein in elem.iter():
                ap_tag = (
                    alt_protein.tag.split("}")[-1]
                    if "}" in alt_protein.tag
                    else alt_protein.tag
                )
                if ap_tag == "alternative_protein":
                    alt_p = alt_protein.get("protein", "")
                    if alt_p and alt_p not in proteins:
                        proteins.append(alt_p)

            calc_mass = float(elem.get("calc_neutral_pep_mass", 0))
            mass_diff = precursor_mass - calc_mass

            num_matched = int(elem.get("num_matched_ions", 0))
            num_total = int(elem.get("tot_num_ions", 0))

            is_decoy = any(self._decoy_prefix in p for p in proteins)

            # Get modification info
            modified_peptide = peptide
            for mod_info in elem.iter():
                mi_tag = (
                    mod_info.tag.split("}")[-1]
                    if "}" in mod_info.tag
                    else mod_info.tag
                )
                if mi_tag == "modification_info":
                    modified_peptide = mod_info.get("modified_peptide", peptide)
                    break

            # Get scores from search_score elements
            scores: Dict[str, float] = {}
            for score_elem in elem.iter():
                se_tag = (
                    score_elem.tag.split("}")[-1]
                    if "}" in score_elem.tag
                    else score_elem.tag
                )
                if se_tag == "search_score":
                    name = score_elem.get("name", "")
                    try:
                        value = float(score_elem.get("value", 0))
                        scores[name] = value
                    except ValueError:
                        pass

            # Get analysis results (q-value, PEP from PeptideProphet, etc.)
            q_value = None
            pep = None
            for analysis in elem.iter():
                an_tag = (
                    analysis.tag.split("}")[-1]
                    if "}" in analysis.tag
                    else analysis.tag
                )
                if an_tag == "peptideprophet_result":
                    prob = float(analysis.get("probability", 0))
                    pep = 1.0 - prob
                elif an_tag == "analysis_result":
                    for child in analysis:
                        ch_tag = (
                            child.tag.split("}")[-1]
                            if "}" in child.tag
                            else child.tag
                        )
                        if ch_tag == "peptideprophet_result":
                            prob = float(child.get("probability", 0))
                            pep = 1.0 - prob

            # Use primary score (prefer hyperscore, xcorr, or first available)
            score = scores.get("hyperscore", scores.get("xcorr", scores.get("expect", 0)))
            if score == 0 and scores:
                score = list(scores.values())[0]

            return PSM(
                scan_number=scan_number,
                peptide=peptide,
                charge=charge,
                score=score,
                proteins=proteins,
                retention_time=retention_time,
                precursor_mz=precursor_mz,
                precursor_mass=precursor_mass,
                mass_diff=mass_diff,
                modified_peptide=modified_peptide,
                num_matched_ions=num_matched,
                num_total_ions=num_total,
                q_value=q_value,
                pep=pep,
                rank=rank,
                is_decoy=is_decoy,
                extra=scores,
            )

        except (ValueError, KeyError):
            return None

    def filter_to_file(self, output_path: Union[str, Path], scan_numbers: Set[int]) -> None:
        """
        Write a subset of PSMs matching the given scan numbers to a new file.

        Args:
            output_path: Path to the output file.
            scan_numbers: Set of scan numbers to include.
        """
        # Get decompressed data from source
        data = self._source.read()
        
        # Use iterparse of XML data
        context = ET.iterparse(io.BytesIO(data), events=("start", "end"))
        context = iter(context)
        event, root = next(context) # get root element
        
        with open(output_path, 'wb') as f:
            # Parse the whole tree to handle filtering
            tree = ET.parse(io.BytesIO(data))
            root = tree.getroot()
            
            # Helper to check scan number
            def get_scan(elem):
                spectrum = elem.get("spectrum", "")
                scan_match = re.search(r"\.(\d+)\.", spectrum)
                if scan_match:
                    return int(scan_match.group(1))
                return int(elem.get("start_scan", 0))

            def strip_ns(tag):
                return tag.split("}")[-1] if "}" in tag else tag
                
            # Iterate over parents that can contain spectrum_query (usually msms_run_summary)
            for parent in root.iter():
                # Find children to remove
                to_remove = []
                for child in parent:
                    if strip_ns(child.tag) == "spectrum_query":
                        scan = get_scan(child)
                        if scan not in scan_numbers:
                            to_remove.append(child)
                
                for child in to_remove:
                    parent.remove(child)
            
            tree.write(f, encoding='UTF-8', xml_declaration=True)
