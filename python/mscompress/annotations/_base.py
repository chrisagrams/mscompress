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
    Union,
    overload,
)

from ._types import PSM


class BaseSearchResultsReader(ABC):
    """
    Abstract base class for search results readers.

    Provides iteration and indexing over PSMs, with lookup by scan number.

    Subclasses must implement:
    - _parse(): Parse the file and populate _psms
    """

    def __init__(self, file_path: Union[str, Path], **kwargs: Any):
        """
        Initialize the reader.

        Args:
            file_path: Path to the search results file.
            **kwargs: Additional format-specific arguments.
        """
        self._file_path = Path(file_path)
        if not self._file_path.exists():
            raise FileNotFoundError(f"Search results file not found: {self._file_path}")

        self._psms: List[PSM] = []
        self._scan_index: Dict[int, List[int]] = {}  # scan_number -> [psm indices]
        self._parsed = False
        self._iter_index = 0

    @property
    def file_path(self) -> Path:
        """Path to the search results file."""
        return self._file_path

    @property
    @abstractmethod
    def format(self) -> str:
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

    def _build_scan_index(self) -> None:
        """Build index mapping scan numbers to PSM indices."""
        self._scan_index.clear()
        for idx, psm in enumerate(self._psms):
            if psm.scan_number not in self._scan_index:
                self._scan_index[psm.scan_number] = []
            self._scan_index[psm.scan_number].append(idx)

    def __enter__(self) -> BaseSearchResultsReader:
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
