"""The base class every search-results format implements."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import ClassVar, Optional, Tuple

from mscompress.annotations._io import Source, load
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat


class SearchResultsFormat(ABC):
    """
    A search-results file format that can be read into a :class:`PSMTable`.

    ``read()`` handles paths, bytes and zstd for every format; subclasses
    implement ``_parse()`` for their own content, plus ``sniff()`` so the
    format can be recognized without a file name. Reader options (decoy
    prefix, rank cutoff, ...) are constructor arguments.

    Example:
        ```python
        table = PepXML(max_rank=2).read("results.pepXML")
        MzTab().write(table, "results.mztab")
        ```
    """

    format: ClassVar[AnnotationFormat]
    #: File name suffixes (lowercase, without any ``.zst``).
    extensions: ClassVar[Tuple[str, ...]] = ()

    def read(self, source: Source) -> PSMTable:
        """
        Read search results.

        Args:
            source: Path to the file, or its contents. zstd-compressed input
                is decompressed.
        """
        data, name = load(source)
        return self._read(data, name)

    def _read(self, data: bytes, name: Optional[str]) -> PSMTable:
        table = self._parse(data)
        if table.metadata.source_file is None:
            table.metadata.source_file = name
        return table

    @abstractmethod
    def _parse(self, data: bytes) -> PSMTable:
        """Parse decompressed file contents."""

    @classmethod
    def matches_name(cls, name: str) -> bool:
        """Whether a file name has one of this format's extensions."""
        lower = name.lower()
        if lower.endswith(".zst"):
            lower = lower[:-4]
        return lower.endswith(cls.extensions)

    @classmethod
    def sniff(cls, head: str) -> bool:
        """Whether the start of a file (decoded text) looks like this format."""
        return False
