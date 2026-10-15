"""Format detection and the :func:`read_psms` entry point."""

from __future__ import annotations

from typing import Optional, Tuple, Type

from mscompress.annotations._io import Source, load
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.mztab import MzTab
from mscompress.annotations.pepxml import PepXML
from mscompress.annotations.percolator import Percolator
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat

#: Every readable format, in detection order.
FORMATS: Tuple[Type[SearchResultsFormat], ...] = (MzTab, PepXML, Percolator)


def detect_format(data: bytes, name: Optional[str] = None) -> Optional[Type[SearchResultsFormat]]:
    """The format of a file, from its name and then its content."""
    if name:
        for fmt in FORMATS:
            if fmt.matches_name(name):
                return fmt
    head = data[:65536].decode("utf-8", errors="ignore")
    return next((fmt for fmt in FORMATS if fmt.sniff(head)), None)


def read_psms(source: Source, format: Optional[AnnotationFormat] = None) -> PSMTable:
    """
    Read search results in any supported format, with default options.

    Args:
        source: Path to a pepXML, Percolator (.tsv/.pin) or mzTab file, or
            its contents. zstd-compressed input is decompressed.
        format: Skip detection and read as this format.

    Raises:
        ValueError: If the format can't be detected or isn't supported.
    """
    data, name = load(source)
    if format is not None:
        fmt = next((f for f in FORMATS if f.format == format), None)
    else:
        fmt = detect_format(data, name)
    if fmt is None:
        raise ValueError(f"Unsupported or undetected search results format: {name or 'bytes'}")
    return fmt()._read(data, name)
