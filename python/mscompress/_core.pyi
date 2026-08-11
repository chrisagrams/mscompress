"""A versatile compression tool for efficient management of mass-spectrometry data."""

from typing import Union, Iterator, Optional, Dict, Any
from os import PathLike
from xml.etree.ElementTree import Element
import numpy as np
import numpy.typing as npt

from .types import AlgorithmInfo, SpectrumTransform

class RuntimeArguments:
    """Runtime configuration arguments for compression/decompression operations."""

    threads: int
    blocksize: int
    mz_lossy: str
    int_lossy: str
    mz_scale_factor: int
    int_scale_factor: int
    target_xml_format: int
    target_mz_format: int
    target_inten_format: int
    zstd_compression_level: int
    shuffle: bool  # on by default

    def __init__(self) -> None: ...

class DataFormat:
    """Data format information for mzML/MSZ files."""
    
    @property
    def source_mz_fmt(self) -> int: ...
    
    @property
    def source_inten_fmt(self) -> int: ...
    
    @property
    def source_compression(self) -> int: ...
    
    @property
    def source_total_spec(self) -> int: ...
    
    @property
    def target_xml_format(self) -> int: ...
    
    @property
    def target_mz_format(self) -> int: ...
    
    @property
    def target_inten_format(self) -> int: ...
    
    def __str__(self) -> str: ...
    
    def to_dict(self) -> Dict[str, Union[str, int]]: ...

class DataPositions:
    """Position information for data blocks in files."""
    
    @property
    def start_positions(self) -> npt.NDArray[np.uint64]: ...
    
    @property
    def end_positions(self) -> npt.NDArray[np.uint64]: ...
    
    @property
    def total_spec(self) -> int: ...

class Division:
    """Division structure containing data positions and scan information."""
    
    @property
    def spectra(self) -> DataPositions: ...
    
    @property
    def xml(self) -> DataPositions: ...
    
    @property
    def mz(self) -> DataPositions: ...
    
    @property
    def inten(self) -> DataPositions: ...
    
    @property
    def size(self) -> int: ...
    
    @property
    def scans(self) -> npt.NDArray[np.uint32]: ...
    
    @property
    def ms_levels(self) -> npt.NDArray[np.uint16]: ...
    
    @property
    def ret_times(self) -> Optional[npt.NDArray[np.float32]]: ...

class BaseFile:
    """Base class for mzML and MSZ file handlers."""
    
    @property
    def path(self) -> bytes: ...
    
    @property
    def filesize(self) -> int: ...
    
    @property
    def format(self) -> DataFormat: ...
    
    @property
    def spectra(self) -> Spectra: ...
    
    @property
    def positions(self) -> Division: ...
    
    @property
    def arguments(self) -> RuntimeArguments: ...
    
    def __enter__(self) -> BaseFile: ...
    
    def __exit__(
        self,
        exc_type: Optional[type],
        exc_value: Optional[BaseException],
        traceback: Optional[Any]
    ) -> None: ...
    
    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]: ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]: ...
    
    def get_xml(self, index: int) -> Element: ...
    
    def describe(self) -> Dict[str, Any]: ...
    
    def compress(self, output: Union[str, PathLike]) -> MSZFile: ...

    def decompress(self, output: Union[str, PathLike]) -> MZMLFile: ...

    def compress_stream(self, chunk_size: int = ...) -> Iterator[bytes]:
        """Compress this file and yield the MSZ output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of compressed MSZ data.
        """
        ...

    def decompress_stream(self, chunk_size: int = ...) -> Iterator[bytes]:
        """Decompress this file and yield the mzML output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of decompressed mzML data.
        """
        ...

    def extract_stream(
        self,
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None,
        chunk_size: int = ...,
    ) -> Iterator[bytes]:
        """Extract spectra and yield the mzML output as byte chunks.

        Args:
            indicies: List of spectrum indices to extract (optional).
            scan_numbers: List of scan numbers to extract (optional).
            ms_level: MS level to extract (optional).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of extracted mzML data.
        """
        ...

    def extract(
            self,
            output: Union[str, PathLike],
            indicies: Optional[list[int]] = None,
            scan_numbers: Optional[list[int]] = None,
            ms_level: Optional[int] = None
    ) -> Union[MZMLFile, MSZFile]:
        """
        Extract specific spectra from a file to a new mzML or MSZ file.

        Parameters:
            output: Output file path (string or path-like), must end in .msz or .mzML.
            indicies: List of spectrum indices to extract (optional).
            scan_numbers: List of scan numbers to extract (optional).
            ms_level: MS level to extract (optional).
        """
        ...

    
    def get_header(self) -> str:
        """
        Extract the complete mzML header as a raw string.
        
        This function extracts the header portion of an mzML file (everything from the start
        of the file to the first spectrum element).
        
        Returns:
            The raw XML header string.
            
        Raises:
            RuntimeError: If header extraction fails.
        """
        ...
    
    def extract_metadata(self, tag_name: str) -> Element:
        """
        Extract and parse a specific XML tag from the mzML file header.
        
        This method extracts the header portion of an mzML file, searches for a specific
        XML tag (e.g., 'referenceableParamGroupList', 'cvList', 'fileDescription'), 
        strips any content outside of it, and parses that XML element.
        
        Parameters:
            tag_name: The name of the XML tag to extract (without namespace).
            
        Returns:
            An xml.etree.ElementTree.Element containing the parsed XML tag.
            
        Raises:
            ValueError: If the tag is not found in the header.
            RuntimeError: If header extraction fails.
            ParseError: If XML parsing fails.
        """
        ...

class MZMLFile(BaseFile):
    """Handler for mzML format files."""

    def __init__(self, path: bytes) -> None: ...

    def compress(self, output: Union[str, PathLike]) -> MSZFile:
        """
        Compress an mzML file to MSZ format.

        Parameters:
            output: Output file path (string or path-like).
        """
        ...

    def compress_stream(self, chunk_size: int = ...) -> Iterator[bytes]:
        """Compress this mzML file and yield the MSZ output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of compressed MSZ data.
        """
        ...

    def extract(self, output: str | PathLike, indicies: list[int] | None = ..., scan_numbers: list[int] | None = ..., ms_level: int | None = ...) -> Union[MZMLFile, MSZFile]: ...
    """
    Extract specific spectra from an mzML file to a new mzML or MSZ file.

    Parameters:
        output: Output file path (string or path-like), must end in .msz or .mzML.
        indicies: List of spectrum indices to extract (optional).
        scan_numbers: List of scan numbers to extract (optional).
        ms_level: MS level to extract (optional).
    """

    def extract_stream(self, indicies: list[int] | None = ..., scan_numbers: list[int] | None = ..., ms_level: int | None = ..., chunk_size: int = ...) -> Iterator[bytes]:
        """Extract spectra from this mzML file and yield mzML output as byte chunks.

        Args:
            indicies: List of spectrum indices to extract (optional).
            scan_numbers: List of scan numbers to extract (optional).
            ms_level: MS level to extract (optional).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of extracted mzML data.
        """
        ...

    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract m/z binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of m/z values.
        """
        ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract intensity binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of intensity values.
        """
        ...
    
    def get_xml(self, index: int) -> Element:
        """
        Extract XML element for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            ElementTree Element containing spectrum metadata.
        """
        ...

class MSZFile(BaseFile):
    """Handler for MSZ (compressed) format files."""

    def __init__(self, path: bytes) -> None: ...

    def decompress(self, output: Union[str, PathLike]) -> MZMLFile:
        """
        Decompress an MSZ file to mzML format.

        Parameters:
            output: Output file path (string or path-like).
        """
        ...

    def decompress_stream(self, chunk_size: int = ...) -> Iterator[bytes]:
        """Decompress this MSZ file and yield the mzML output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of decompressed mzML data.
        """
        ...

    def extract(self, output: str | PathLike, indicies: list[int] | None = ..., scan_numbers: list[int] | None = ..., ms_level: int | None = ...) -> Union[MZMLFile, MSZFile]: ...
    """
    Extract specific spectra from an MSZ file to a new mzML or MSZ file.
    Parameters:
        output: Output file path (string or path-like).
        indicies: List of spectrum indices to extract (optional).
        scan_numbers: List of scan numbers to extract (optional).
        ms_level: MS level to extract (optional).
    """

    def extract_stream(self, indicies: list[int] | None = ..., scan_numbers: list[int] | None = ..., ms_level: int | None = ..., chunk_size: int = ...) -> Iterator[bytes]:
        """Extract spectra from this MSZ file and yield mzML output as byte chunks.

        Args:
            indicies: List of spectrum indices to extract (optional).
            scan_numbers: List of scan numbers to extract (optional).
            ms_level: MS level to extract (optional).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            Chunks of extracted mzML data.
        """
        ...
    
    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract m/z binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of m/z values.
        """
        ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract intensity binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of intensity values.
        """
        ...
    
    def get_xml(self, index: int) -> Element:
        """
        Extract XML element for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            ElementTree Element containing spectrum metadata.
        """
        ...

class MSZXFile(MSZFile):
    """First-class reader for MSZX (tar) archives.

    Extends MSZFile so all spectrum properties and methods work directly on
    the embedded MSZ payload, while adding access to the archive manifest and
    bundled annotation readers. Open via the `open()` classmethod.
    """

    _closed: bool

    @classmethod
    def open(cls, path: Union[str, PathLike]) -> "MSZXFile":
        """Open an MSZX archive for reading.

        Mmaps the embedded MSZ payload directly out of the archive (no temp
        extraction) and eagerly reads annotation entries into memory.

        Args:
            path: Path to the .mszx file.
        """
        ...

    def close(self) -> None:
        """Close the archive and release all resources. Idempotent."""
        ...

    @property
    def manifest(self) -> Any:
        """The archive manifest (`MSZXManifest`)."""
        ...

    @property
    def archive_path(self) -> Any:
        """Path to the MSZX archive."""
        ...

    @property
    def annotations(self) -> Any:
        """Primary annotation reader, or None if no annotations."""
        ...

    @property
    def annotation_readers(self) -> Dict[str, Any]:
        """Dict of all annotation readers, keyed by filename."""
        ...

    @property
    def annotation_files(self) -> list:
        """List of annotation entries in the archive."""
        ...

    def get_annotation_reader(self, filename: str) -> Any:
        """Get the annotation reader for a specific file by name."""
        ...

    def get_annotation_readers_by_format(self, format: str) -> list:
        """Get all annotation readers matching a specific format."""
        ...

    def describe(self) -> Dict[str, Any]:
        """Description including the archive metadata."""
        ...

    def decompress(self, output: Union[str, PathLike]) -> MZMLFile:
        """Decompress the embedded MSZ to mzML.

        If `output` is an existing directory or has no file extension, the
        mzML is written inside it as `<archive_stem>.mzML` and any cached
        annotation readers are written alongside. Otherwise `output` is
        treated as a single file path and only the mzML is written.
        """
        ...

    def extract(self, output: str | PathLike, indicies: list[int] | None = ..., scan_numbers: list[int] | None = ..., ms_level: int | None = ...) -> "MSZXFile":
        """Extract a subset of spectra and annotations to a new MSZX archive.

        Args:
            output: Path to the output .mszx file.
            indicies: List of spectrum indices to extract.
            scan_numbers: List of scan numbers to extract.
            ms_level: Filter by MS level.

        Returns:
            New MSZXFile instance for the created archive.
        """
        ...

class Spectrum:
    """Represents a single mass spectrum."""

    index: int
    scan: int
    ms_level: int

    def __init__(
        self,
        index: int,
        scan: int,
        ms_level: int,
        retention_time: float,
        file: BaseFile,
        transform: Optional[SpectrumTransform] = None,
    ) -> None: ...

    def __repr__(self) -> str: ...

    @property
    def xml(self) -> Element:
        """XML metadata for this spectrum."""
        ...

    @property
    def size(self) -> int:
        """Number of m/z - intensity pairs (pre-transform)."""
        ...

    @property
    def retention_time(self) -> float:
        """Retention time of this spectrum."""
        ...

    @property
    def raw_mz(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Raw m/z values array (before any transform)."""
        ...

    @property
    def raw_intensity(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Raw intensity values array (before any transform)."""
        ...

    @property
    def mz(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """m/z values array (after transform, if set)."""
        ...

    @property
    def intensity(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Intensity values array (after transform, if set)."""
        ...

    @property
    def peaks(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Combined m/z and intensity as 2D array (after transform, if set)."""
        ...

class Spectra:
    """
    Collection of spectra with lazy loading and iteration support.

    Allows indexing and iteration over all spectra in a file.
    """

    def __init__(
        self,
        f: BaseFile,
        df: DataFormat,
        positions: Division
    ) -> None: ...

    def __iter__(self) -> Iterator[Spectrum]: ...

    def __next__(self) -> Spectrum: ...

    def __getitem__(self, index: int) -> Spectrum: ...

    def __len__(self) -> int: ...

    def set_transform(self, transform: Optional[SpectrumTransform]) -> None:
        """Set a transform function applied lazily to all spectra.

        Parameters:
            transform: A callable (SpectrumDict) -> SpectrumDict, or None to clear.
        """
        ...

    def with_transform(self, transform: Optional[SpectrumTransform]) -> Spectra:
        """Return a new Spectra with the given transform, without modifying this one.

        Parameters:
            transform: A callable (SpectrumDict) -> SpectrumDict, or None.

        Returns:
            A new Spectra instance with the transform set.
        """
        ...

def get_num_threads() -> int:
    """
    Get the number of available threads on the system.
    
    Returns:
        Number of usable threads.
    """
    ...

def get_filesize(path: Union[str, bytes]) -> int:
    """
    Get the size of a file in bytes.

    Parameters:
        path: Path to file (string or bytes).

    Returns:
        Size of the file in bytes.

    Raises:
        FileNotFoundError: If file does not exist.
    """
    ...

def list_algorithms() -> list[AlgorithmInfo]:
    """Return a list of available lossy algorithm descriptors from the C registry."""
    ...


class MSZXBatchWriter:
    """Incremental writer for a v2 multi-file ("batch") .mszx archive.

    Wraps the same C writer the CLI drives, so an archive written from Python
    is byte-identical to one the CLI produces from the same inputs and
    settings. Entries stream straight into the archive file — no temp .msz
    staging — which is why the output must be a seekable regular file.

    The archive only becomes valid once `finish()` succeeds. Leaving the
    `with` block via an exception calls `abort()`, removing the partial file.
    """

    def __init__(self, path: Union[str, bytes, PathLike], **kwargs: Any) -> None:
        """Open an archive for writing.

        Args:
            path: Output .mszx path. Must be a seekable regular file.
            **kwargs: Compression settings forwarded to RuntimeArguments
                (threads, blocksize, mz_lossy, int_lossy, mz_scale_factor,
                int_scale_factor, target_xml_format, target_mz_format,
                target_inten_format, zstd_compression_level).
        """
        ...

    def add(
        self, source: Union[str, bytes, PathLike, MZMLFile], name: Optional[str] = None
    ) -> int:
        """Compress one mzML into the archive as a new entry.

        Args:
            source: mzML to compress. An already-open MZMLFile reuses its
                existing mapping.
            name: Entry name inside the archive. Defaults to
                "<source basename minus .mzML>.msz"; collisions get __2, __3,
                ... suffixes. Naming is handled by the C writer so the CLI and
                every binding agree.

        Returns:
            Index of the new entry, for add_annotation/set_join_key.
        """
        ...

    def add_annotation(
        self,
        entry_index: int,
        data: bytes,
        filename: str,
        format: str = "tsv",
        compressed: bool = False,
        num_records: Optional[int] = None,
    ) -> None:
        """Attach an annotation file to a spectra entry, as its own member.

        Compression is the caller's job — pass already-compressed bytes and
        set `compressed=True`.
        """
        ...

    def set_join_key(self, entry_index: int, join_key: str) -> None:
        """Set the column used to join annotations to spectra for one entry."""
        ...

    def set_description(self, description: str) -> None:
        """Set an archive-level free-text description."""
        ...

    def set_extra(self, extra: dict) -> None:
        """Set the archive-level `extra` metadata object."""
        ...

    def finish(self) -> None:
        """Write manifest.json and the end-of-archive marker, then close.

        On failure the partial archive is removed. The writer is unusable
        afterwards either way.
        """
        ...

    def abort(self) -> None:
        """Discard the archive, removing the partial file. Idempotent."""
        ...

    @property
    def path(self) -> Union[str, bytes]:
        """Output archive path."""
        ...

    @property
    def entries(self) -> list:
        """Source paths added so far, in archive order."""
        ...

    def __len__(self) -> int: ...
    def __enter__(self) -> "MSZXBatchWriter": ...
    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> bool: ...
