"""Generic search results reader."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Optional, Union

from ._base import BaseSearchResultsReader
from .percolator import PINReader
from .pepxml import PepXMLReader


class SearchResultsReader:
    """
    Generic search results reader.

    This class auto-detects the file format and returns the appropriate
    reader implementation.

    Example:
        >>> reader = SearchResultsReader("results.pin")
        >>> for psm in reader:
        ...     print(psm.peptide, psm.score)
        >>>
        >>> with SearchResultsReader("results.pepXML") as reader:
        ...     psms = reader.get_by_scan(1234)
    """

    def __new__(
        cls,
        file_path: Union[str, Path],
        format: Optional[str] = None,
        **kwargs: Any,
    ) -> BaseSearchResultsReader:
        """
        Create appropriate reader based on file format.

        Args:
            file_path: Path to the search results file.
            format: Format hint ('pin', 'pepxml'). Auto-detected if None.
            **kwargs: Additional arguments passed to the reader constructor.

        Returns:
            Appropriate reader instance for the file format.

        Raises:
            FileNotFoundError: If the file doesn't exist.
            ValueError: If the format cannot be determined.
        """
        path = Path(file_path)
        if not path.exists():
            raise FileNotFoundError(f"Search results file not found: {path}")

        suffix = path.suffix.lower()

        if format is None:
            # Auto-detect format
            if suffix == ".pin":
                format = "pin"
            elif suffix in (".pepxml", ".pep.xml"):
                format = "pepxml"
            elif suffix == ".xml":
                # Check if it's pepXML by reading first few lines
                with open(path, "r") as f:
                    header = f.read(1024)
                    if "pepXML" in header or "spectrum_query" in header:
                        format = "pepxml"

        if format == "pin":
            return PINReader(path, **kwargs)
        elif format == "pepxml":
            return PepXMLReader(path, **kwargs)
        else:
            raise ValueError(
                f"Cannot determine format for {path}. "
                "Please specify format='pin' or 'pepxml'."
            )
