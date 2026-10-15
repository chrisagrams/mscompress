"""Format detection and the :func:`read_psms` entry point."""

from __future__ import annotations

import re
from typing import Optional

from mscompress.annotations._io import Source, load
from mscompress.annotations.mztab import read_mztab
from mscompress.annotations.pepxml import read_pepxml
from mscompress.annotations.percolator import read_percolator
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat

_READERS = {
    AnnotationFormat.MZTAB: read_mztab,
    AnnotationFormat.PEPXML: read_pepxml,
    AnnotationFormat.PERCOLATOR_TSV: read_percolator,
}


def detect_format(data: bytes, name: Optional[str] = None) -> Optional[AnnotationFormat]:
    """Guess a search-results format from its file name, then its content."""
    lower = (name or "").lower()
    if lower.endswith(".zst"):
        lower = lower[:-4]
    if lower.endswith(".mztab"):
        return AnnotationFormat.MZTAB
    if lower.endswith((".pepxml", ".pep.xml")):
        return AnnotationFormat.PEPXML
    if lower.endswith((".pin", ".tsv")):
        return AnnotationFormat.PERCOLATOR_TSV

    head = data[:65536].decode("utf-8", errors="ignore")
    if head.startswith(("MTD\t", "COM\t")) or "\nPSH\t" in head:
        return AnnotationFormat.MZTAB
    if re.search(r"<(\w+:)?(msms_pipeline_analysis|spectrum_query)\b", head):
        return AnnotationFormat.PEPXML
    first_line = head.split("\n", 1)[0]
    if any(col in first_line.split("\t") for col in ("PSMId", "SpecId", "ScanNr")):
        return AnnotationFormat.PERCOLATOR_TSV
    return None


def read_psms(source: Source, format: Optional[AnnotationFormat] = None) -> PSMTable:
    """
    Read search results in any supported format.

    Args:
        source: Path to a pepXML, Percolator (.tsv/.pin) or mzTab file, or
            its contents. zstd-compressed input is decompressed.
        format: Skip detection and read as this format.

    Raises:
        ValueError: If the format can't be detected or isn't supported.
    """
    data, name = load(source)
    fmt = format or detect_format(data, name)
    reader = _READERS.get(fmt) if fmt is not None else None
    if reader is None:
        raise ValueError(f"Unsupported or undetected search results format: {name or 'bytes'}")
    table = reader(data)
    table.metadata.source_file = table.metadata.source_file or name
    return table
