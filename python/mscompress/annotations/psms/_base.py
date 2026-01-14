"""Base classes for search results readers.

This module defines the abstract base class for reading peptide identification
results from various file formats, and a generic reader with format auto-detection.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from pathlib import Path
from typing import (
    Any,
    Dict,
    Iterator,
    List,
    Optional,
    Set,
    TypeVar,
    Union,
    overload,
)

from mscompress.annotations._base import BaseAnnotationFile, PathAnnotationFile
from mscompress.annotations.psms._types import PSM
from mscompress.types import AnnotationFormat


T = TypeVar("T", bound="BasePSMReader")


class BasePSMReader(ABC):
    """
    Abstract base class for search results readers.

    Provides iteration and indexing over PSMs, with lookup by scan number.
    
    Supports reading from:
    - File paths (compressed or uncompressed with zstd)
    - Tar archive members (via AnnotationSource)
    - Raw bytes data

    Subclasses must implement:
    - _parse(): Parse the file and populate _psms
    """

    def __init__(
        self,
        source: Union[str, Path, BaseAnnotationFile],
        **kwargs: Any,
    ):
        """
        Initialize the reader.

        Args:
            source: Source for reading - file path or AnnotationSource.
            **kwargs: Additional format-specific arguments.
        """
        if isinstance(source, BaseAnnotationFile):
            self._source = source
            self._file_path = source.path
        else:
            path = Path(source)
            # For file paths, validate existence
            if not path.exists():
                raise FileNotFoundError(f"Search results file not found: {path}")
            self._source = PathAnnotationFile(path)
            self._file_path = path

        self._psms: List[PSM] = []
        self._scan_index: Dict[int, List[int]] = {}  # scan_number -> [psm indices]
        self._parsed = False
        self._iter_index = 0
        
    @property
    def source(self) -> BaseAnnotationFile:
        """The annotation source for this reader."""
        return self._source

    @property
    def file_path(self) -> Optional[Path]:
        """Path to the search results file (if from a file path)."""
        return self._file_path
    
    @property
    def name(self) -> Optional[str]:
        """Name of this annotation source."""
        return self._source.name

    @property
    @abstractmethod
    def format(self) -> AnnotationFormat:
        """Return the format identifier for this reader (e.g., 'pin', 'pepxml')."""
        ...

    @property
    def psms(self) -> List[PSM]:
        """List of all PSMs (triggers parsing if needed)."""
        self._ensure_parsed()
        return self._psms

    def _ensure_parsed(self) -> None:
        """Ensure the file has been parsed."""
        if not self._parsed:
            self._parse()
            self._build_scan_index()
            self._parsed = True

    @abstractmethod
    def _parse(self) -> None:
        """
        Parse the file and populate self._psms.

        Must be implemented by subclasses.
        """
        ...

    @abstractmethod
    def filter_to_file(self, output_path: Union[str, Path], scan_numbers: Set[int]) -> None:
        """
        Write a subset of PSMs matching the given scan numbers to a new file.

        Args:
            output_path: Path to the output file.
            scan_numbers: Set of scan numbers to include.
        """
        ...

    def _build_scan_index(self) -> None:
        """Build index mapping scan numbers to PSM indices."""
        self._scan_index.clear()
        for idx, psm in enumerate(self._psms):
            if psm.scan_number not in self._scan_index:
                self._scan_index[psm.scan_number] = []
            self._scan_index[psm.scan_number].append(idx)

    def __enter__(self) -> BasePSMReader:
        """Enter context manager."""
        self._ensure_parsed()
        return self

    def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> None:
        """Exit context manager."""
        pass

    def __len__(self) -> int:
        self._ensure_parsed()
        return len(self._psms)

    @overload
    def __getitem__(self, index: int) -> PSM: ...

    @overload
    def __getitem__(self, index: slice) -> List[PSM]: ...

    def __getitem__(self, index: Union[int, slice]) -> Union[PSM, List[PSM]]:
        self._ensure_parsed()
        return self._psms[index]

    def __contains__(self, item: object) -> bool:
        self._ensure_parsed()
        return item in self._psms

    def __iter__(self) -> Iterator[PSM]:
        self._ensure_parsed()
        self._iter_index = 0
        return self

    def __next__(self) -> PSM:
        if self._iter_index >= len(self._psms):
            raise StopIteration
        psm = self._psms[self._iter_index]
        self._iter_index += 1
        return psm

    def get_by_scan(self, scan_number: int) -> List[PSM]:
        """
        Get all PSMs for a given scan number.

        Args:
            scan_number: The scan number to look up.

        Returns:
            List of PSMs matching the scan number (may be empty).
        """
        self._ensure_parsed()
        indices = self._scan_index.get(scan_number, [])
        return [self._psms[i] for i in indices]

    def get_best_by_scan(self, scan_number: int) -> Optional[PSM]:
        """
        Get the best-scoring PSM for a given scan number.

        Args:
            scan_number: The scan number to look up.

        Returns:
            Best PSM by score, or None if no matches.
        """
        psms = self.get_by_scan(scan_number)
        if not psms:
            return None
        return max(psms, key=lambda p: p.score)

    def has_scan(self, scan_number: int) -> bool:
        """Check if a scan number has any PSMs."""
        self._ensure_parsed()
        return scan_number in self._scan_index

    @property
    def scan_numbers(self) -> List[int]:
        """List of all unique scan numbers with PSMs."""
        self._ensure_parsed()
        return list(self._scan_index.keys())
