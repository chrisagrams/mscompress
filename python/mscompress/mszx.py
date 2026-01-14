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
from dataclasses import dataclass, field
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

from ._core import MSZFile

from .types import AnnotationFormat

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
            "annotations": [sr.to_dict() for sr in self.annotations],
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
        self.msz = msz
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
        self.msz = msz_file
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

            msz_file = MSZFile(str(msz_path).encode())
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

    def get_annotation_readers_by_format(self, format: AnnotationFormat) -> List[BasePSMReader]:
        """
        Get all annotation readers matching a specific format.

        Args:
            format: The annotation format to filter by.

        Returns:
            List of BasePSMReader instances matching the format.
        """
        
        readers = []
        for entry in self._manifest.annotations:
            if entry.format == format and entry.filename in self._annotations:
                readers.append(self._annotations[entry.filename])
        return readers

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
        return self.msz.path

    @property
    def filesize(self) -> int:
        """Size of the underlying MSZ file."""
        return self.msz.filesize

    @property
    def format(self) -> DataFormat:
        """Data format information."""
        return self.msz.format

    @property
    def spectra(self) -> Spectra:
        """Collection of spectra with lazy loading."""
        return self.msz.spectra

    @property
    def positions(self) -> Division:
        """Position information for data blocks."""
        return self.msz.positions

    @property
    def arguments(self) -> RuntimeArguments:
        """Runtime configuration arguments."""
        return self.msz.arguments

    def get_mz_binary(
        self, index: int
    ) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Extract m/z binary array for a spectrum at the given index."""
        return self.msz.get_mz_binary(index)

    def get_inten_binary(
        self, index: int
    ) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Extract intensity binary array for a spectrum at the given index."""
        return self.msz.get_inten_binary(index)

    def get_xml(self, index: int) -> Element:
        """Extract XML element for a spectrum at the given index."""
        return self.msz.get_xml(index)
    
    def describe(self) -> Dict[str, Any]:
        """Get description of the file."""
        desc = self.msz.describe()
        desc["archive"] = {
            "path": str(self._archive_path),
            "annotations": [sr.to_dict() for sr in self._manifest.annotations],
        }
        return desc

    def get_header(self) -> str:
        """Extract the complete mzML header as a raw string."""
        return self.msz.get_header()

    def extract_metadata(self, tag_name: str) -> Element:
        """Extract and parse a specific XML tag from the mzML file header."""
        return self.msz.extract_metadata(tag_name)
    
    def decompress(self, output: Union[str, os.PathLike]) -> None:
        """Decompress the MSZ file to mzML format."""
        self.msz.decompress(output)

    def extract(
        self,
        output: Union[str, Path],
        indicies: Optional[List[int]] = None,
        scan_numbers: Optional[List[int]] = None,
        ms_level: Optional[int] = None,
    ) -> MSZXFile:
        """
        Extract a subset of spectra and annotations to a new MSZX archive.

        Args:
            output: Path to the output .mszx file.
            indicies: List of spectrum indices to extract.
            scan_numbers: List of scan numbers to extract.
            ms_level: Filter by MS level.

        Returns:
            New MSZXFile instance for the created archive.
        """
        output_path = Path(output)
        if not output_path.suffix:
            output_path = output_path.with_suffix(".mszx")

        # Set containing scans based on filters
        target_scans: set[int] = set()

        # If scan numbers provided, populate target_scans
        if scan_numbers:
            target_scans.update(scan_numbers)
        
        # If indices provided, map to scan numbers
        if indicies:
            all_scans = self.msz.positions.scans
            for idx in indicies:
                if 0 <= idx < len(all_scans):
                    target_scans.add(int(all_scans[idx]))

        # If ms_level provided, filter all spectra
        if ms_level is not None:
            all_scans = self.msz.positions.scans
            all_levels = self.msz.positions.ms_levels
             
            for i in range(len(all_scans)):
                # Check ms_level match
                if all_levels[i] == ms_level:
                    # Check if it also matches index/scan constraints if they exist
                    scan = int(all_scans[i])
                    is_match = True
                    
                    if indicies and i not in indicies:
                        is_match = False
                
                    if scan_numbers and scan not in scan_numbers:
                        is_match = False
                        
                    if is_match:
                        target_scans.add(scan)
                        

            # If both indicies and scan_numbers provided, we need to intersect
            if indicies or scan_numbers:
                valid_scans_by_level = set()
                for i in range(len(all_scans)):
                    if all_levels[i] == ms_level:
                        valid_scans_by_level.add(int(all_scans[i]))
                        
                target_scans.intersection_update(valid_scans_by_level)
        
            else:
                # No indices/scans provided, so we take ALL with matching ms_level
                for i in range(len(all_scans)):
                    if all_levels[i] == ms_level:
                        target_scans.add(int(all_scans[i]))

        # If no filters provided at all, we extract everything
        if not indicies and not scan_numbers and ms_level is None:
            all_scans = self.msz.positions.scans
            target_scans.update([int(s) for s in all_scans])

        # Convert final target list back to list for C function
        final_scan_list = list(target_scans)
        final_scan_list.sort() # Sort for deterministic behavior

        # Extract MSZ to temp file
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            temp_msz_path = temp_path / "temp.msz"
            
            # Use the MSZ extract w/ final scan list
            self.msz.extract(
                output=str(temp_msz_path),
                scan_numbers=final_scan_list
            )
            
            extracted_msz = MSZFile(str(temp_msz_path).encode('utf-8'))
            
            try:
                # Create new MSZX archive
                builder = MSZXBuilder(extracted_msz, compression=True)
                builder.set_description(self.manifest.description or "")
                
                # Copy extra metadata
                for k, v in self.manifest.extra.items():
                    builder.set_extra(k, v)
                    
                # Filter and add annotations
                for filename, reader in self._annotations.items():
                    # Create temp file for filtered annotation
                    fname_path = Path(filename)
                    if fname_path.suffix.lower() == '.zst':
                        temp_filename = fname_path.stem
                    else:
                        temp_filename = filename
                        
                    temp_ann_path = temp_path / temp_filename
                    
                    # Check directly if supported
                    try:
                        reader.filter_to_file(temp_ann_path, target_scans)
                        
                        # Add to builder
                        # Create a new reader for the filtered file
                        new_reader = PSMReader(temp_ann_path)
                        builder.add_annotations(new_reader)
                        
                    except Exception as e:
                            warnings.warn(f"Failed to filter annotation {filename}: {e}")
        
                # Save final archive
                builder.save(output_path)
            finally:
                # Ensure extracted_msz is closed before temp directory cleanup (important on Windows)
                extracted_msz._cleanup()
            
            # Return new instance
            return MSZXFile.open(output_path)


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