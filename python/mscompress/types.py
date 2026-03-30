from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Optional, TYPE_CHECKING
from enum import Enum

if TYPE_CHECKING:
    import numpy as np
    from numpy.typing import NDArray


class SpectrumDict:
    """TypedDict-like specification for the dict passed to spectrum transforms.

    Keys:
        mz: NDArray[np.float64] — raw m/z values
        intensity: NDArray[np.float64] — raw intensity values
    """
    __slots__ = ()

    # Provided for documentation; actual dicts are plain dicts at runtime.
    mz: NDArray[np.float64]
    intensity: NDArray[np.float64]


class SpectrumTransform:
    """Protocol-style base describing the callable signature for transforms.

    A spectrum transform is any callable with the signature::

        (dict with keys "mz" and "intensity") -> dict with keys "mz" and "intensity"

    You do **not** need to subclass this; any callable matching the signature works.
    """
    __slots__ = ()

    def __call__(self, data: dict) -> dict:
        raise NotImplementedError


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