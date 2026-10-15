"""MSZX file format handler.

MSZX is a bundled archive format that combines:
- Compressed mass spectrometry data (MSZ)
- Search results, stored as mzTab (converted from pepXML, Percolator, ...)
- Internal manifest for self-description (`MSZXManifest`)

This package provides the writer-side machinery (`MSZXBuilder`,
`create_mszx`) and re-exports the reader (`MSZXFile`, defined in the
Cython core) plus the manifest dataclass (`MSZXManifest`, defined in
the leaf submodule `mscompress.mszx.metadata`).
"""

from __future__ import annotations

import io
import tarfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import zstandard as zstd

from mscompress._core import MSZFile, MSZXFile  # ty: ignore[unresolved-import]
from mscompress.annotations import (
    PSM,
    BasePSMReader,
    PSMReader,
    SearchInfo,
    mztab_member_name,
    reader_to_mztab,
    write_mztab,
)
from mscompress.annotations._base import DEFAULT_ZSTD_COMPRESSION_LEVEL
from mscompress.mszx.batch import MSZXBatchFile, compress_batch
from mscompress.mszx.metadata import (
    MSZXBatchManifest,
    MSZXManifest,
    SpectraFileEntry,
)
from mscompress.types import AnnotationEntry, AnnotationFormat

__all__ = [
    "MSZXBatchFile",
    "compress_batch",
    "MSZXBatchManifest",
    "MSZXBuilder",
    "MSZXFile",
    "MSZXManifest",
    "SpectraFileEntry",
    "create_mszx",
]


class MSZXBuilder:
    """
    Builder for creating MSZX archives from MSZ files and annotations.

    Example:
        ```python
        msz = mscompress.read("sample.msz")
        builder = MSZXBuilder(msz)
        builder.add_annotations(reader, description="Percolator results")
        builder.set_description("Proteomics dataset with PSM annotations")
        builder.save("sample.mszx")
        ```
    """

    def __init__(
        self,
        msz: MSZFile,
        source_name: Optional[str] = None,
        compression: bool = True,
    ):
        """
        Initialize the builder with an MSZ file.

        Args:
            msz: MSZFile object containing spectra.
            source_name: Optional source file name for provenance.
        """
        self.msz = msz
        path_str = msz.path.decode("utf-8")
        self.compression = compression
        self._msz_path = Path(path_str)
        self._annotations: List[tuple[bytes, AnnotationEntry]] = []
        self._description: Optional[str] = None
        self._source_name = source_name or self._msz_path.name
        self._join_key = "spectra_ref"
        self._extra: Dict[str, Any] = {}

    def add_annotations(
        self,
        reader: BasePSMReader,
        description: Optional[str] = None,
    ) -> MSZXBuilder:
        """
        Add search results to the archive, stored as mzTab.

        Engine output (pepXML, Percolator TSV, ...) is converted to mzTab;
        the original file is not stored. mzTab input is stored unchanged.

        Args:
            reader: BasePSMReader instance containing the PSMs.
            description: Optional description of the annotations.
        Returns:
            Self for method chaining.
        """
        mztab = reader_to_mztab(reader, ms_run_location=self._msz_path.name)
        source_name = reader.name or f"annotations{len(self._annotations) + 1}"
        return self._add_mztab(
            mztab.encode("utf-8"),
            mztab_member_name(source_name, self.compression),
            len(reader),
            description,
        )

    def add_psms(
        self,
        psms: List[PSM],
        filename: str,
        search_info: Optional[SearchInfo] = None,
        description: Optional[str] = None,
    ) -> MSZXBuilder:
        """
        Add in-memory PSMs to the archive, stored as mzTab.

        Args:
            psms: PSMs to store.
            filename: Base name for the archive member (``.mztab`` is appended).
            search_info: Optional search metadata for the mzTab header.
            description: Optional description of the annotations.
        Returns:
            Self for method chaining.
        """
        mztab = write_mztab(
            psms, search_info=search_info, ms_run_location=self._msz_path.name
        )
        return self._add_mztab(
            mztab.encode("utf-8"),
            mztab_member_name(filename, self.compression),
            len(psms),
            description,
        )

    def _add_mztab(
        self,
        data: bytes,
        filename: str,
        num_records: int,
        description: Optional[str],
    ) -> MSZXBuilder:
        if any(entry.filename == filename for _, entry in self._annotations):
            raise ValueError(f"Duplicate annotation member name: {filename}")
        entry = AnnotationEntry(
            filename=filename,
            format=AnnotationFormat.MZTAB,
            compressed=self.compression,
            description=description,
            num_records=num_records,
        )
        if self.compression:
            data = zstd.ZstdCompressor(level=DEFAULT_ZSTD_COMPRESSION_LEVEL).compress(data)
        self._annotations.append((data, entry))
        return self

    def set_description(self, description: str) -> MSZXBuilder:
        """Set the archive description."""
        self._description = description
        return self

    def set_join_key(self, join_key: str) -> MSZXBuilder:
        """Set the key used to join spectra with search results."""
        self._join_key = join_key
        return self

    def set_extra(self, key: str, value: Any) -> MSZXBuilder:
        """Add extra metadata to the manifest."""
        self._extra[key] = value
        return self

    def _build_manifest(self) -> MSZXManifest:
        """Build the manifest from current state."""
        return MSZXManifest(
            spectra_file=self._msz_path.name,
            num_spectra=len(self.msz.spectra),
            annotations=[entry for _, entry in self._annotations],
            join_key=self._join_key,
            description=self._description,
            source_file=self._source_name,
            extra=self._extra,
        )

    def save(self, output_path: Union[str, Path]) -> Path:
        """
        Save the MSZX archive to disk.

        Args:
            output_path: Output file path (should end with .mszx).

        Returns:
            Path to the created archive.
        """
        output = Path(output_path)
        if not output.suffix:
            output = output.with_suffix(".mszx")

        manifest = self._build_manifest()

        with tarfile.open(output, "w") as tar:
            manifest_bytes = manifest.to_json().encode("utf-8")
            manifest_info = tarfile.TarInfo(name="manifest.json")
            manifest_info.size = len(manifest_bytes)
            tar.addfile(manifest_info, io.BytesIO(manifest_bytes))

            tar.add(str(self._msz_path), arcname=manifest.spectra_file)

            for data, entry in self._annotations:
                annotation_info = tarfile.TarInfo(name=entry.filename)
                annotation_info.size = len(data)
                tar.addfile(annotation_info, io.BytesIO(data))

        return output


def create_mszx(
    msz: MSZFile,
    output_path: Union[str, Path],
    annotations: Optional[List[Union[str, Path, BasePSMReader]]] = None,
    description: Optional[str] = None,
) -> Path:
    """
    Create an MSZX archive from an MSZ file.

    Convenience function for simple archive creation.

    Args:
        msz: MSZFile object.
        output_path: Output path for the .mszx file.
        annotations: List of file paths or readers for annotations.
        description: Optional description for the archive.

    Returns:
        Path to the created archive.

    Example:
        ```python
        msz = mscompress.read("sample.msz")
        create_mszx(
            msz,
            "sample.mszx",
            annotations=["sample.pin", "sample.pepXML"],
            description="Annotated proteomics dataset",
        )
        ```
    """
    builder = MSZXBuilder(msz)

    if description:
        builder.set_description(description)

    if annotations:
        for annotation in annotations:
            if isinstance(annotation, (str, Path)):
                reader = PSMReader(annotation)
                builder.add_annotations(reader)
            elif isinstance(annotation, BasePSMReader):
                builder.add_annotations(annotation)

    return builder.save(output_path)

