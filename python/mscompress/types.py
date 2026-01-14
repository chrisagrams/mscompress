from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Optional
from enum import Enum


class AnnotationFormat(Enum):
    """Supported annotation formats."""

    PERCOLATOR_TSV = "percolator_tsv"
    PEPXML = "pepxml"
    TSV = "tsv"


@dataclass
class AnnotationEntry:
    """Entry describing an annotation file in the archive."""

    filename: str
    format: AnnotationFormat
    compressed: bool
    description: Optional[str] = None
    num_records: Optional[int] = None

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        data: Dict[str, Any] = {
            "filename": self.filename,
            "format": self.format.value,
            "compressed": self.compressed,
        }
        if self.description is not None:
            data["description"] = self.description
        if self.num_records is not None:
            data["num_records"] = self.num_records
        return data

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> AnnotationEntry:
        """Create from dictionary."""
        return cls(
            filename=data["filename"],
            format=AnnotationFormat(data["format"]),
            compressed=data.get("compressed", False),
            description=data.get("description"),
            num_records=data.get("num_records"),
        )