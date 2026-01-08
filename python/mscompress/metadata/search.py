"""
Metadata builders for search result files.

This module provides metadata builders for peptide search result formats
including Percolator (.pin/.pout) and pepXML files.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional, Union

from ._base import MetadataBuilder
from ._types import (
    DataCollectionInfo,
    FieldDefinition,
    FileDistribution,
    RecordSetDefinition,
)


class SearchResultsMetadataBuilder(MetadataBuilder):
    """
    Abstract base class for search results metadata (PSMs, peptide IDs, etc.).

    Search results typically contain peptide-spectrum matches (PSMs) with
    scores, sequences, and other identification data.
    """

    def __init__(
        self,
        file_path: Union[str, Path],
        source_name: Optional[str] = None,
    ):
        """
        Initialize the search results metadata builder.

        Args:
            file_path: Path to the search results file.
            source_name: Optional name override for the file source.
        """
        self._file_path = Path(file_path)
        self._source_name = source_name or f"{self._file_path.stem}-results"

    @property
    def join_key(self) -> str:
        """Search results join on scan_number by default."""
        return "scan_number"


class PercolatorMetadataBuilder(SearchResultsMetadataBuilder):
    """
    Builds metadata from Percolator .pin or .pout files.

    Percolator files contain peptide-spectrum match scores and features
    used for FDR control in proteomics.
    """

    def __init__(
        self,
        file_path: Union[str, Path],
        source_name: Optional[str] = None,
        num_psms: Optional[int] = None,
    ):
        """
        Initialize the Percolator metadata builder.

        Args:
            file_path: Path to .pin or .pout file.
            source_name: Optional name for the source.
            num_psms: Number of PSMs (if known, otherwise computed lazily).
        """
        super().__init__(file_path, source_name)
        self._num_psms = num_psms

    @property
    def name(self) -> str:
        return "percolator_results"

    @property
    def description(self) -> str:
        return "Percolator search results with PSM scores and features"

    def get_record_set(self) -> RecordSetDefinition:
        """Build RecordSet with Percolator-specific fields."""
        fields = [
            FieldDefinition(
                name="psm_id",
                description="Unique identifier for the peptide-spectrum match",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="scan_number",
                description="Scan number linking to spectrum",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="peptide",
                description="Peptide sequence with modifications",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="charge",
                description="Precursor charge state",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="score",
                description="Percolator discriminant score",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="q_value",
                description="Posterior error probability (q-value)",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="pep",
                description="Posterior error probability",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="proteins",
                description="Protein accessions for this peptide",
                data_type="sc:Text",
                source_file_object=self._source_name,
                is_array=True,
            ),
        ]

        return RecordSetDefinition(
            name=self.name,
            description=self.description,
            fields=fields,
            join_key=self.join_key,
        )

    def get_distribution(self, base_url: str) -> FileDistribution:
        return FileDistribution(
            name=self._source_name,
            content_url=f"{base_url}/search_results/",
            encoding_format="text/tab-separated-values",
        )

    def get_data_collection_info(self) -> DataCollectionInfo:
        size_desc = f"{self._num_psms} PSMs" if self._num_psms else "unknown PSMs"
        return DataCollectionInfo(
            size_description=size_desc,
            data_type="peptide-spectrum matches",
            format_description="Percolator PIN/POUT",
        )


class PepXMLMetadataBuilder(SearchResultsMetadataBuilder):
    """
    Builds metadata from pepXML search result files.

    pepXML is a standardized format for peptide identification results
    from database search engines.
    """

    def __init__(
        self,
        file_path: Union[str, Path],
        source_name: Optional[str] = None,
        num_psms: Optional[int] = None,
    ):
        """
        Initialize the pepXML metadata builder.

        Args:
            file_path: Path to .pepXML file.
            source_name: Optional name for the source.
            num_psms: Number of PSMs (if known).
        """
        super().__init__(file_path, source_name)
        self._num_psms = num_psms

    @property
    def name(self) -> str:
        return "pepxml_results"

    @property
    def description(self) -> str:
        return "pepXML peptide identification results"

    def get_record_set(self) -> RecordSetDefinition:
        """Build RecordSet with pepXML-specific fields."""
        fields = [
            FieldDefinition(
                name="spectrum_query",
                description="Spectrum query identifier",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="scan_number",
                description="Scan number linking to spectrum",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="precursor_neutral_mass",
                description="Neutral mass of the precursor ion",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="assumed_charge",
                description="Assumed charge state",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="peptide",
                description="Identified peptide sequence",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="modified_peptide",
                description="Peptide with modification annotations",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="num_matched_ions",
                description="Number of matched fragment ions",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="tot_num_ions",
                description="Total number of theoretical ions",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="calc_neutral_pep_mass",
                description="Calculated neutral peptide mass",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="massdiff",
                description="Mass difference between observed and calculated",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="protein",
                description="Protein accession",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="search_score",
                description="Search engine score (e.g., XCorr, hyperscore)",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
        ]

        return RecordSetDefinition(
            name=self.name,
            description=self.description,
            fields=fields,
            join_key=self.join_key,
        )

    def get_distribution(self, base_url: str) -> FileDistribution:
        return FileDistribution(
            name=self._source_name,
            content_url=f"{base_url}/search_results/",
            encoding_format="application/xml",
        )

    def get_data_collection_info(self) -> DataCollectionInfo:
        size_desc = f"{self._num_psms} PSMs" if self._num_psms else "unknown PSMs"
        return DataCollectionInfo(
            size_description=size_desc,
            data_type="peptide identifications",
            format_description="pepXML",
        )
