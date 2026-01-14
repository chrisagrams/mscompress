"""Annotation file handling for peptide identification files.

This package provides implementations for reading and writing annotation
files with compression support.

The annotation abstraction provides transparent zstd compression/decompression
when reading and writing to archives.

Example:
    Basic usage with auto-detection:

    >>> from mscompress.annotations import PSMReader
    >>> reader = PSMReader("results.pin")
    >>> for psm in reader:
    ...     print(psm.peptide, psm.score)

    With context manager:

    >>> with PSMReader("results.pepXML") as reader:
    ...     for psm in reader:
    ...         print(psm.peptide)

    Using annotation files with compression:

    >>> from mscompress.annotations import AnnotationFile
    >>> # Read a compressed annotation file
    >>> annotation = AnnotationFile.from_file("results.pin.zst")
    >>> for psm in annotation:
    ...     print(psm.peptide, psm.score)
    >>>
    >>> # Create from reader and save compressed
    >>> reader = PINReader("results.pin")
    >>> annotation = AnnotationFile.from_reader(reader)
    >>> annotation.save("results.pin.zst", compress=True)

    Using specific readers:

    >>> from mscompress.annotations import PINReader, PepXMLReader
    >>> pin_reader = PINReader("results.pin")
    >>> pepxml_reader = PepXMLReader("results.pepXML")
"""

from __future__ import annotations

# Core types
from .psms._types import PSM

# PSM Base class
from .psms._base import BasePSMReader

# Generic PSM reader
from .psms.reader import PSMReader

# Specific PSM readers
from .psms.percolator import TSVReader
from .psms.pepxml import PepXMLReader

# Annotation file abstraction
from ._base import (
    BaseAnnotationFile,
    MSZXAnnotationFile,
    PathAnnotationFile,
)


__all__ = [
    # Types
    "PSM",
    # PSM Base class
    "BasePSMReader",
    # Generic PSM reader
    "PSMReader",
    # Specific PSM readers
    "TSVReader",
    "PepXMLReader",
    # Annotation file abstraction
    "BaseAnnotationFile",
    "MSZXAnnotationFile",
    "PathAnnotationFile",
]
