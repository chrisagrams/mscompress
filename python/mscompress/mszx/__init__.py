"""MSZX file format handler.

MSZX is a bundled archive format that combines:
- Compressed mass spectrometry data (MSZ)
- Search results (e.g., Percolator .pin, pepXML)
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

from mscompress._core import MSZFile, MSZXFile  # ty: ignore[unresolved-import]
from mscompress.annotations import BasePSMReader, PSMReader, PathAnnotationFile
from mscompress.mszx.batch import MSZXBatchFile, compress_batch
from mscompress.mszx.metadata import (
    MSZXBatchManifest,
    MSZXManifest,
    SpectraFileEntry,
)
from mscompress.types import AnnotationEntry

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
        self._annotations: List[tuple[Path, AnnotationEntry]] = []
        self._description: Optional[str] = None
        self._source_name = source_name or self._msz_path.name
        self._join_key = "scan_number"
        self._extra: Dict[str, Any] = {}

    def add_annotations(
        self,
        reader: BasePSMReader,
        description: Optional[str] = None,
    ) -> MSZXBuilder:
        """
        Add annotations to the archive.

        Args:
            reader: BasePSMReader instance containing the annotations.
            description: Optional description of the annotations.
        Returns:
            Self for method chaining.
        """
        if reader.file_path is None:
            raise ValueError("Annotation reader must have a file path for archiving")
        path = Path(reader.file_path)
        if not path.exists():
            raise FileNotFoundError(f"Annotation file not found: {path}")

        fmt = reader.format
        num_records = len(reader)

        filename = path.name if not self.compression else path.name + ".zst"

        entry = AnnotationEntry(
            filename=filename,
            format=fmt,
            compressed=self.compression,
            description=description,
            num_records=num_records,
        )
        self._annotations.append((path, entry))
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

            for path, entry in self._annotations:
                if self.compression:
                    annotation_source = PathAnnotationFile(path)
                    compressed_data = annotation_source.get_compressed()
                    annotation_info = tarfile.TarInfo(name=entry.filename)
                    annotation_info.size = len(compressed_data)
                    tar.addfile(annotation_info, io.BytesIO(compressed_data))
                else:
                    tar.add(str(path), arcname=entry.filename)

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
