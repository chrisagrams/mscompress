"""Generic search results reader."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Optional, Union

from mscompress.annotations.psms._base import BasePSMReader, BaseAnnotationFile, PathAnnotationFile
from mscompress.types import AnnotationFormat
from mscompress.annotations.psms.percolator import TSVReader
from mscompress.annotations.psms.pepxml import PepXMLReader


def _detect_format_from_name(name: str) -> Optional[AnnotationFormat]:
    """Detect format from filename/suffix."""
    name_lower = name.lower()
    
    # Remove .zst suffix if present
    if name_lower.endswith(".zst"):
        name_lower = name_lower[:-4]
    
    if name_lower.endswith(".pin"):
        return AnnotationFormat.PERCOLATOR_TSV
    elif name_lower.endswith(".tsv"):
        return AnnotationFormat.PERCOLATOR_TSV
    elif name_lower.endswith(".pepxml") or name_lower.endswith(".pep.xml"):
        return AnnotationFormat.PEPXML
    elif name_lower.endswith(".xml"):
        return None  # Need to check content
    
    return None


def _detect_format_from_content(data: bytes) -> Optional[AnnotationFormat]:
    """Detect format from file content."""
    # Check first KB for format hints
    header = data[:1024].decode("utf-8", errors="ignore")
    
    if "pepXML" in header or "spectrum_query" in header:
        return AnnotationFormat.PEPXML
    elif "SpecId" in header or "PSMId" in header or "ScanNr" in header:
        return AnnotationFormat.PERCOLATOR_TSV
    
    return None


class PSMReader:
    """
    Generic search results reader factory.

    This class auto-detects the file format and returns the appropriate
    reader implementation. Supports reading from file paths, tar archives,
    and raw bytes with transparent zstd decompression.

    Example:
        >>> reader = PSMReader("results.tsv")
        >>> for psm in reader:
        ...     print(psm.peptide, psm.score)
        >>>
        >>> with PSMReader("results.pepXML") as reader:
        ...     psms = reader.get_by_scan(1234)
        >>>
    """

    def __new__(
        cls,
        source: Union[str, Path, BaseAnnotationFile],
        format: Optional[AnnotationFormat] = None,
        **kwargs: Any,
    ) -> BasePSMReader:
        """
        Create appropriate reader based on file format.

        Args:
            source: Source for reading - file path or BaseAnnotationFile.
            format: Format hint ('pin', 'pepxml'). Auto-detected if None.
            **kwargs: Additional arguments passed to the reader constructor.

        Returns:
            Appropriate reader instance for the file format.

        Raises:
            FileNotFoundError: If the file doesn't exist (for file paths).
            ValueError: If the format cannot be determined.
        """
        # Convert to AnnotationSource if needed
        if isinstance(source, BaseAnnotationFile):
            ann_source = source
        else:
            path = Path(source)
            if not path.exists():
                raise FileNotFoundError(f"Search results file not found: {path}")
            ann_source = PathAnnotationFile(path)
        
        # Try to detect format
        if format is None:
            # First try by name
            name = ann_source.name
            if name:
                format = _detect_format_from_name(name)
            
            # If still unknown, check content
            if format is None:
                data = ann_source.read()
                format = _detect_format_from_content(data)

        if format == AnnotationFormat.PERCOLATOR_TSV:
            return TSVReader(ann_source, **kwargs)
        elif format == AnnotationFormat.PEPXML:
            return PepXMLReader(ann_source, **kwargs)
        else:
            name = ann_source.name or "unknown"
            raise ValueError(
                f"Cannot determine format for {name}. "
                "Please specify format='percolator_tsv' or 'pepxml'."
            )