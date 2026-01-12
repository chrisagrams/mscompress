"""
Metadata builder for MSZ (compressed mzML) files.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Optional

from ._base import MetadataBuilder
from ._types import (
    DataCollectionInfo,
    FieldDefinition,
    FileDistribution,
    RecordSetDefinition,
)

if TYPE_CHECKING:
    from .._core import MSZFile


class MSZMetadataBuilder(MetadataBuilder):
    """
    Builds Croissant metadata from MSZ (compressed mzML) files.

    Extracts spectrum-level metadata including m/z arrays, intensities,
    retention times, and scan information.
    """

    def __init__(self, msz: MSZFile, source_name: Optional[str] = None):
        """
        Initialize the MSZ metadata builder.

        Args:
            msz: MSZFile to extract metadata from.
            source_name: Optional name override for the file source.
        """
        self._msz = msz
        self._source_name = source_name or "msz-files"
        self._num_spectra = len(msz.spectra)

    @property
    def name(self) -> str:
        return "mass_spectra"

    @property
    def description(self) -> str:
        return "Mass spectrometry spectra with m/z and intensity data"

    @property
    def join_key(self) -> str:
        return "scan_number"

    def get_record_set(self) -> RecordSetDefinition:
        """Build RecordSet with all spectrum fields."""
        fields = [
            FieldDefinition(
                name="source_file",
                description="Name of the source MSZ file",
                data_type="sc:Text",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="spectrum_index",
                description="Zero-based index of the spectrum within the file",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="scan_number",
                description="Scan number from the mass spectrometry instrument",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="ms_level",
                description="MS level (1 for MS1, 2 for MS2/MS3, etc.)",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="retention_time",
                description="Retention time in seconds",
                data_type="sc:Float",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="num_peaks",
                description="Number of m/z-intensity pairs in the spectrum",
                data_type="sc:Integer",
                source_file_object=self._source_name,
            ),
            FieldDefinition(
                name="mz",
                description="Array of mass-to-charge ratio (m/z) values",
                data_type="sc:Float",
                source_file_object=self._source_name,
                is_array=True,
            ),
            FieldDefinition(
                name="intensity",
                description="Array of intensity values corresponding to m/z values",
                data_type="sc:Float",
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
            content_url=f"{base_url}/data/",
            encoding_format="application/octet-stream",
        )

    def get_data_collection_info(self) -> DataCollectionInfo:
        return DataCollectionInfo(
            size_description=f"{self._num_spectra} spectra",
            data_type="mass spectrometry data",
            format_description="MSZ (compressed mzML)",
        )
