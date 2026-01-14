"""
Abstract base class for metadata builders.

This module defines the interface that all metadata builders must implement.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Optional

from ._types import DataCollectionInfo, FileDistribution, RecordSetDefinition


class MetadataBuilder(ABC):
    """
    Abstract base class for building Croissant metadata from data sources.

    Subclasses implement specific metadata extraction for different file types
    (MSZ files, search results, etc.).
    """

    @property
    @abstractmethod
    def name(self) -> str:
        """Unique identifier for this metadata source."""
        ...

    @property
    @abstractmethod
    def description(self) -> str:
        """Human-readable description of this metadata source."""
        ...

    @abstractmethod
    def get_record_set(self) -> RecordSetDefinition:
        """Build the RecordSet definition for this metadata source."""
        ...

    @abstractmethod
    def get_distribution(self, base_url: str) -> FileDistribution:
        """Build the file distribution definition."""
        ...

    @abstractmethod
    def get_data_collection_info(self) -> DataCollectionInfo:
        """Build the data collection info."""
        ...

    @property
    def join_key(self) -> Optional[str]:
        """
        The field name to use for joining with other metadata sources.
        Override in subclasses to enable joining.
        """
        return None
