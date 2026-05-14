"""MSZXManifest dataclass — the JSON manifest stored inside an .mszx archive.

Lives in its own leaf submodule so it can be imported by `_core.pyx`
(via `from mscompress.mszx.metadata import MSZXManifest`) without dragging
in the rest of the `mscompress.mszx` package machinery.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Dict, List, Optional

from mscompress.types import AnnotationEntry


@dataclass
class MSZXManifest:
    """
    Manifest describing the contents of an MSZX archive.

    This is stored as manifest.json inside the archive for self-description.
    """

    version: str = "1.0"
    created_at: str = field(default_factory=lambda: datetime.now(timezone.utc).isoformat())
    spectra_file: str = "spectra.msz"
    num_spectra: int = 0
    annotations: List[AnnotationEntry] = field(default_factory=list)
    join_key: str = "scan_number"
    description: Optional[str] = None
    source_file: Optional[str] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary for JSON serialization."""
        data: Dict[str, Any] = {
            "version": self.version,
            "created_at": self.created_at,
            "spectra_file": self.spectra_file,
            "num_spectra": self.num_spectra,
            "annotations": [sr.to_dict() for sr in self.annotations],
            "join_key": self.join_key,
        }
        if self.description:
            data["description"] = self.description
        if self.source_file:
            data["source_file"] = self.source_file
        if self.extra:
            data["extra"] = self.extra
        return data

    def to_json(self, indent: int = 2) -> str:
        """Serialize to JSON string."""
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> MSZXManifest:
        """Create from dictionary."""
        annotations = [
            AnnotationEntry.from_dict(sr) for sr in data.get("annotations", [])
        ]
        return cls(
            version=data.get("version", "1.0"),
            created_at=data.get("created_at", ""),
            spectra_file=data.get("spectra_file", "spectra.msz"),
            num_spectra=data.get("num_spectra", 0),
            annotations=annotations,
            join_key=data.get("join_key", "scan_number"),
            description=data.get("description"),
            source_file=data.get("source_file"),
            extra=data.get("extra", {}),
        )

    @classmethod
    def from_json(cls, json_str: str) -> MSZXManifest:
        """Parse from JSON string."""
        return cls.from_dict(json.loads(json_str))
