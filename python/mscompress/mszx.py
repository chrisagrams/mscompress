"""
MSZX file format handler.

MSZX is a bundled archive format that combines:
- Compressed mass spectrometry data (MSZ)
- Search results (e.g., Percolator .pin, pepXML)
- Internal manifest for self-description
"""

from __future__ import annotations

import io
import json
import os
import shutil
import tarfile
import tempfile
import warnings
from dataclasses import asdict, dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import (
    TYPE_CHECKING,
    Any,
    Dict,
    List,
    Optional,
    Union,
)
from xml.etree.ElementTree import Element

import numpy as np
import numpy.typing as npt

from ._core import MSZFile, read
from .annotations import (
    BasePSMReader,
    PSMReader,
    MSZXAnnotationFile,
    PathAnnotationFile,
)

from .types import AnnotationEntry

if TYPE_CHECKING:
    from ._core import (
        DataFormat,
        Division,
        RuntimeArguments,
        Spectra,
    )


@dataclass
class MSZXManifest:
    """
    Manifest describing the contents of an MSZX archive.

    This is stored as manifest.json inside the archive for self-description.
    """

    version: str = "1.0"
    created_at: str = field(default_factory=lambda: datetime.now(timezone.utc).isoformat())
    spectra_file: str = "spectra.msz"
    num_spectra: int = 0
    annotations: List[AnnotationEntry] = field(default_factory=list)
    join_key: str = "scan_number"
    description: Optional[str] = None
    source_file: Optional[str] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        data = {
            "version": self.version,
            "created_at": self.created_at,
            "spectra_file": self.spectra_file,
            "num_spectra": self.num_spectra,
            "annotations": [asdict(sr) for sr in self.annotations],
            "join_key": self.join_key,
        }
        if self.description:
            data["description"] = self.description
        if self.source_file:
            data["source_file"] = self.source_file
        if self.extra:
            data["extra"] = self.extra
        return data

    def to_json(self, indent: int = 2) -> str:
        """Serialize to JSON string."""
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> MSZXManifest:
        """Create from dictionary."""
        annotations = [
            AnnotationEntry.from_dict(sr) for sr in data.get("annotations", [])
        ]
        return cls(
            version=data.get("version", "1.0"),
            created_at=data.get("created_at", ""),
            spectra_file=data.get("spectra_file", "spectra.msz"),
            num_spectra=data.get("num_spectra", 0),
            annotations=annotations,
            join_key=data.get("join_key", "scan_number"),
            description=data.get("description"),
            source_file=data.get("source_file"),
            extra=data.get("extra", {}),
        )

    @classmethod
    def from_json(cls, json_str: str) -> MSZXManifest:
        """Parse from JSON string."""
        return cls.from_dict(json.loads(json_str))


class MSZXBuilder:
    """
    Builder for creating MSZX archives from MSZ files and annotations.

    Example:
        >>> msz = mscompress.read("sample.msz")
        >>> builder = MSZXBuilder(msz)
        >>> builder.add_annotations(reader, description="Percolator results")
        >>> builder.set_description("Proteomics dataset with PSM annotations")
        >>> builder.save("sample.mszx")
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
        self._msz = msz
        path_str = msz.path.decode("utf-8") if isinstance(msz.path, bytes) else str(msz.path)
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
        # Get path from reader
        if reader.file_path is None:
            raise ValueError("Annotation reader must have a file path for archiving")
        path = Path(reader.file_path)
        if not path.exists():
            raise FileNotFoundError(f"Annotation file not found: {path}")

        # Automatically detect format and count PSMs from reader
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
            num_spectra=len(self._msz.spectra),
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
            # Add manifest
            manifest_bytes = manifest.to_json().encode("utf-8")
            manifest_info = tarfile.TarInfo(name="manifest.json")
            manifest_info.size = len(manifest_bytes)
            tar.addfile(manifest_info, io.BytesIO(manifest_bytes))

            # Add MSZ file
            tar.add(str(self._msz_path), arcname=manifest.spectra_file)

            # Add annotations
            for path, entry in self._annotations:
                if self.compression:
                    # Use PathAnnotationFile to handle compression
                    annotation_source = PathAnnotationFile(path)
                    compressed_data = annotation_source.get_compressed()
                    
                    # Add compressed data to archive
                    compressed_filename = entry.filename
                    annotation_info = tarfile.TarInfo(name=compressed_filename)
                    annotation_info.size = len(compressed_data)
                    tar.addfile(annotation_info, io.BytesIO(compressed_data))
                else:
                    # Add uncompressed file directly
                    tar.add(str(path), arcname=entry.filename)                    

        return output


class MSZXFile:
    """
    Handler for MSZX bundled archive files.

    Provides access to spectra via the same interface as MSZFile,
    plus access to bundled search results with PSM lookup.

    Example:
        >>> with MSZXFile.open("sample.mszx") as mszx:
        ...     print(f"Spectra: {len(mszx.spectra)}")
        ...     
        ...     # Access spectra with optional PSM lookup
        ...     for spectrum in mszx.spectra[:10]:
        ...         psms = mszx.get_psms_for_spectrum(spectrum)
        ...         print(spectrum.scan, len(psms), "PSMs")
        ...     
        ...     # Direct annotations access
        ...     for annotation in mszx.annotations:
        ...         print(annotation.peptide, annotation.score)
    """

    def __init__(
        self,
        archive_path: Union[str, Path],
        manifest: MSZXManifest,
        msz_file: MSZFile,
        temp_dir: Path,
        annotation_readers: Optional[Dict[str, BasePSMReader]] = None,
    ):
        """
        Initialize MSZXFile.

        Args:
            archive_path: Path to the .mszx archive.
            manifest: Parsed manifest.
            msz_file: Extracted MSZ file handler.
            temp_dir: Temporary directory containing extracted files.
            annotation_readers: Dict mapping filenames to annotation readers.
        """
        self._archive_path = Path(archive_path)
        self._manifest = manifest
        self._msz = msz_file
        self._temp_dir = temp_dir
        self._closed = False
        self._annotations: Dict[str, BasePSMReader] = annotation_readers or {}
        self._primary_annotation_reader: Optional[BasePSMReader] = None

        # Set primary annotation reader (first one)
        if self._annotations:
            self._primary_annotation_reader = next(iter(self._annotations.values()))

    @classmethod
    def open(cls, path: Union[str, Path]) -> MSZXFile:
        """
        Open an MSZX archive for reading.

        Args:
            path: Path to the .mszx file.

        Returns:
            MSZXFile instance.

        Raises:
            FileNotFoundError: If the archive doesn't exist.
            ValueError: If the archive is invalid or missing manifest.
        """
        archive_path = Path(path)
        if not archive_path.exists():
            raise FileNotFoundError(f"MSZX file not found: {archive_path}")

        # Create temp directory for extraction (only MSZ file needs to be extracted)
        temp_dir = Path(tempfile.mkdtemp(prefix="mszx_"))

        try:
            with tarfile.open(archive_path, "r") as tar:
                # Extract manifest first
                try:
                    manifest_member = tar.getmember("manifest.json")
                except KeyError:
                    raise ValueError("Invalid MSZX archive: missing manifest.json")

                manifest_file = tar.extractfile(manifest_member)
                if manifest_file is None:
                    raise ValueError("Could not read manifest.json")

                manifest = MSZXManifest.from_json(manifest_file.read().decode("utf-8"))

                # Extract only the MSZ file (it needs to be on disk for the core reader)
                try:
                    msz_member = tar.getmember(manifest.spectra_file)
                    tar.extract(msz_member, temp_dir)
                except KeyError:
                    raise ValueError(
                        f"Invalid MSZX archive: missing spectra file {manifest.spectra_file}"
                    )

                # Read annotation files directly from tar using AnnotationSource
                annotation_readers: Dict[str, BasePSMReader] = {}
                for entry in manifest.annotations:
                    try:
                        member = tar.getmember(entry.filename)
                        mszx_source = MSZXAnnotationFile(
                            mszx_file=tar,
                            member=member,
                            name=entry.filename,
                        )
                        
                        reader = PSMReader(mszx_source)
                        annotation_readers[entry.filename] = reader
                    except KeyError:
                        warnings.warn(
                            f"Annotation file '{entry.filename}' not found in archive",
                            UserWarning,
                            stacklevel=2
                        )
                    except (ValueError, Exception) as e:
                        # Skip files we can't parse, but print a warning
                        warnings.warn(
                            f"Could not parse annotation file '{entry.filename}': {e}",
                            UserWarning,
                            stacklevel=2
                        )

            # Open the MSZ file
            msz_path = temp_dir / manifest.spectra_file
            if not msz_path.exists():
                raise ValueError(
                    f"Invalid MSZX archive: missing spectra file {manifest.spectra_file}"
                )

            msz_file = read(str(msz_path))
            if not isinstance(msz_file, MSZFile):
                raise ValueError(
                    f"Spectra file {manifest.spectra_file} is not a valid MSZ file"
                )

            return cls(
                archive_path=archive_path,
                manifest=manifest,
                msz_file=msz_file,
                temp_dir=temp_dir,
                annotation_readers=annotation_readers,
            )

        except Exception:
            # Clean up temp dir on failure
            shutil.rmtree(temp_dir, ignore_errors=True)
            raise

    def close(self) -> None:
        """Close the archive and clean up temporary files."""
        if not self._closed:
            self._closed = True
            shutil.rmtree(self._temp_dir, ignore_errors=True)

    def __enter__(self) -> MSZXFile:
        return self

    def __exit__(
        self,
        exc_type: Optional[type],
        exc_value: Optional[BaseException],
        traceback: Optional[Any],
    ) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()

    @property
    def archive_path(self) -> Path:
        """Path to the MSZX archive."""
        return self._archive_path

    @property
    def manifest(self) -> MSZXManifest:
        """The archive manifest."""
        return self._manifest

    @property
    def annotation_files(self) -> List[AnnotationEntry]:
        """List of annotation entries in the archive."""
        return self._manifest.annotations

    def get_annotation_reader(self, filename: str) -> BasePSMReader:
        """
        Get the annotation reader for a specific file.

        Args:
            filename: Name of the annotation file.

        Returns:
            BasePSMReader for the annotation file.

        Raises:
            KeyError: If the file is not in the archive.
        """
        if filename in self._annotations:
            return self._annotations[filename]
        raise KeyError(f"Annotation file not found: {filename}")

    @property
    def annotations(self) -> Optional[BasePSMReader]:
        """
        Primary annotation reader.

        Returns the first annotation reader, or None if no annotations.
        """
        return self._primary_annotation_reader

    @property
    def annotation_readers(self) -> Dict[str, BasePSMReader]:
        """Dict of all annotation readers, keyed by filename."""
        return self._annotations


    @property
    def path(self) -> bytes:
        """Path to the underlying MSZ file."""
        return self._msz.path

    @property
    def filesize(self) -> int:
        """Size of the underlying MSZ file."""
        return self._msz.filesize

    @property
    def format(self) -> DataFormat:
        """Data format information."""
        return self._msz.format

    @property
    def spectra(self) -> Spectra:
        """Collection of spectra with lazy loading."""
        return self._msz.spectra

    @property
    def positions(self) -> Division:
        """Position information for data blocks."""
        return self._msz.positions

    @property
    def arguments(self) -> RuntimeArguments:
        """Runtime configuration arguments."""
        return self._msz.arguments

    def get_mz_binary(
        self, index: int
    ) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Extract m/z binary array for a spectrum at the given index."""
        return self._msz.get_mz_binary(index)

    def get_inten_binary(
        self, index: int
    ) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Extract intensity binary array for a spectrum at the given index."""
        return self._msz.get_inten_binary(index)

    def get_xml(self, index: int) -> Element:
        """Extract XML element for a spectrum at the given index."""
        return self._msz.get_xml(index)

    def describe(self) -> Dict[str, Any]:
        """Get description of the file."""
        desc = self._msz.describe()
        desc["archive"] = {
            "path": str(self._archive_path),
            "annotations": [asdict(sr) for sr in self._manifest.annotations],
        }
        return desc

    def get_header(self) -> str:
        """Extract the complete mzML header as a raw string."""
        return self._msz.get_header()

    def extract_metadata(self, tag_name: str) -> Element:
        """Extract and parse a specific XML tag from the mzML file header."""
        return self._msz.extract_metadata(tag_name)

    def decompress(self, output: Union[str, os.PathLike]) -> None:
        """Decompress the MSZ file to mzML format."""
        self._msz.decompress(output)


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
        >>> msz = mscompress.read("sample.msz")
        >>> create_mszx(
        ...     msz,
        ...     "sample.mszx",
        ...     annotations=["sample.pin", "sample.pepXML"],
        ...     description="Annotated proteomics dataset"
        ... )
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