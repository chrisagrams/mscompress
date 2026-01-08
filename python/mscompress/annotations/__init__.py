"""Search results readers for peptide identification files.

This package provides implementations for reading peptide-spectrum match
(PSM) data from various search result formats.

Supported formats:
- Percolator PIN files
- pepXML files

Example:
    Basic usage with auto-detection:

    >>> from mscompress.annotations import SearchResultsReader
    >>> reader = SearchResultsReader("results.pin")
    >>> for psm in reader:
    ...     print(psm.peptide, psm.score)

    With context manager:

    >>> with SearchResultsReader("results.pepXML") as reader:
    ...     for psm in reader:
    ...         print(psm.peptide)

    Using specific readers:

    >>> from mscompress.annotations import PINReader, PepXMLReader
    >>> pin_reader = PINReader("results.pin")
    >>> pepxml_reader = PepXMLReader("results.pepXML")
"""

from __future__ import annotations

# Core types
from ._types import PSM

# Base class
from ._base import BaseSearchResultsReader

# Generic reader with auto-detection
from .reader import SearchResultsReader

# Specific readers
from .percolator import PINReader
from .pepxml import PepXMLReader


__all__ = [
    # Types
    "PSM",
    # Base class
    "BaseSearchResultsReader",
    # Generic reader (auto-detects format)
    "SearchResultsReader",
    # Specific readers
    "PINReader",
    "PepXMLReader",
]
