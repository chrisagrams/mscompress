from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Optional
from enum import Enum


class AnnotationFormat(Enum):
    """Supported annotation formats."""

    PIN = "pin"
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