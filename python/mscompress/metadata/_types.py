"""
Core data structures for Croissant metadata.

This module contains the fundamental data classes used to define
metadata fields, record sets, file distributions, and data collection info.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Dict, List, Optional, Union


class JoinStrategy(Enum):
    """Strategy for joining metadata from multiple sources."""

    SCAN_NUMBER = "scan_number"
    SPECTRUM_INDEX = "spectrum_index"
    RETENTION_TIME = "retention_time"


@dataclass
class FieldDefinition:
    """Definition of a field in a Croissant RecordSet."""

    name: str
    description: str
    data_type: Union[str, List[str]]
    source_file_object: str
    is_array: bool = False

    def to_croissant(self) -> Dict[str, Any]:
        """Convert to Croissant field format."""
        dtype = [self.data_type] if self.is_array else self.data_type
        return {
            "@type": "ml:Field",
            "name": self.name,
            "description": self.description,
            "dataType": dtype,
            "source": {
                "fileObject": self.source_file_object,
                "extract": {"column": self.name},
            },
        }


@dataclass
class RecordSetDefinition:
    """Definition of a RecordSet in Croissant metadata."""

    name: str
    description: str
    fields: List[FieldDefinition] = field(default_factory=list)
    join_key: Optional[str] = None

    def to_croissant(self) -> Dict[str, Any]:
        """Convert to Croissant RecordSet format."""
        return {
            "@type": "ml:RecordSet",
            "name": self.name,
            "description": self.description,
            "field": [f.to_croissant() for f in self.fields],
        }


@dataclass
class FileDistribution:
    """Definition of a file distribution in Croissant metadata."""

    name: str
    content_url: str
    encoding_format: str = "application/octet-stream"
    sha256: str = "unknown"

    def to_croissant(self) -> Dict[str, Any]:
        """Convert to Croissant FileObject format."""
        return {
            "@type": "sc:FileObject",
            "name": self.name,
            "contentUrl": self.content_url,
            "encodingFormat": self.encoding_format,
            "sha256": self.sha256,
        }


@dataclass
class DataCollectionInfo:
    """Information about the data collection."""

    size_description: str
    data_type: str
    format_description: str

    def to_croissant(self) -> Dict[str, Any]:
        """Convert to Croissant DataCollection format."""
        return {
            "@type": "ml:DataCollection",
            "ml:datasetSize": self.size_description,
            "ml:dataType": self.data_type,
            "ml:format": self.format_description,
        }


@dataclass
class JoinDefinition:
    """Defines how two record sets should be joined."""

    left_record_set: str
    right_record_set: str
    join_key: str
    join_type: str = "left"  # left, right, inner, outer

    def to_croissant(self) -> Dict[str, Any]:
        """Convert to Croissant join definition format."""
        return {
            "@type": "ml:Join",
            "ml:left": self.left_record_set,
            "ml:right": self.right_record_set,
            "ml:joinKey": self.join_key,
            "ml:joinType": self.join_type,
        }
