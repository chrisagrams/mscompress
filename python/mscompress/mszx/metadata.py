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


def _manifest_major(data: Dict[str, Any]) -> int:
    """Leading integer of the manifest ``version``; 1 if unparseable."""
    version = data.get("version", "1.0")
    try:
        return int(str(version).split(".", 1)[0])
    except (ValueError, TypeError):
        return 1  # unparseable version -> treat as legacy, proceed


def _reject_unsupported_major(data: Dict[str, Any], max_major: int) -> None:
    major = _manifest_major(data)
    if major > max_major:
        raise ValueError(
            f"MSZX manifest version {data.get('version')!r} is newer than this "
            f"build supports (max major {max_major}); please upgrade mscompress"
        )


def is_batch_manifest(data: Dict[str, Any]) -> bool:
    """True if `data` describes a v2 multi-file ("batch") archive.

    Keyed on the payload shape rather than the version string alone, so a
    mislabeled ``"1.0"`` manifest carrying ``spectra_files`` is still routed to
    the batch reader instead of being mis-read as single-file.
    """
    return "spectra_files" in data or data.get("container") == "batch"


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

    # Highest MSZX manifest major version this binding can read.
    #
    # Deliberately NOT annotated: any annotated assignment in a dataclass body
    # becomes a *field*, which would let it be passed to __init__ and then be
    # silently dropped by to_dict(). `ClassVar[int]` does not help here either —
    # under `from __future__ import annotations` the annotation is a string and
    # dataclasses still registers it as a field on CPython 3.12.
    MAX_SUPPORTED_MAJOR = 2

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> MSZXManifest:
        """Create from dictionary.

        Raises:
            ValueError: if the manifest is a newer major version than this
                build supports, or if it is a multi-file/batch archive — those
                carry N spectra files and must go through
                :class:`MSZXBatchManifest` instead. Refusing loudly avoids
                silently mis-reading a batch archive as a single-file one.
        """
        _reject_unsupported_major(data, cls.MAX_SUPPORTED_MAJOR)

        if is_batch_manifest(data):
            raise ValueError(
                "this .mszx is a multi-file (batch) archive; parse it with "
                "MSZXBatchManifest.from_dict() and open it with MSZXBatchFile"
            )

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


@dataclass
class SpectraFileEntry:
    """One member of a v2 ("batch") MSZX archive.

    Attributes:
        entry: Tar member name of the MSZ payload (e.g. ``"sample.msz"``).
        original: Basename of the source mzML. Only the basename is recorded,
            so the source directory is not recoverable from the archive.
        size: Payload size in bytes, matching the tar header.
        num_spectra: Spectrum count, or ``None`` when the writer omitted it
            (archives produced before this field was added).
        join_key: Column used to join annotations to spectra.
        annotations: Annotation members attached to this entry.
    """

    entry: str
    original: str = ""
    size: int = 0
    num_spectra: Optional[int] = None
    join_key: str = "scan_number"
    annotations: List[AnnotationEntry] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "entry": self.entry,
            "original": self.original,
            "size": self.size,
        }
        if self.num_spectra is not None:
            data["num_spectra"] = self.num_spectra
        if self.join_key:
            data["join_key"] = self.join_key
        if self.annotations:
            data["annotations"] = [a.to_dict() for a in self.annotations]
        return data

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> SpectraFileEntry:
        return cls(
            entry=data["entry"],
            original=data.get("original", ""),
            size=data.get("size", 0),
            num_spectra=data.get("num_spectra"),
            join_key=data.get("join_key", "scan_number"),
            annotations=[
                AnnotationEntry.from_dict(a) for a in data.get("annotations", [])
            ],
        )


@dataclass
class MSZXBatchManifest:
    """Manifest of a v2 multi-file ("batch") MSZX archive.

    A batch archive bundles N independent MSZ files written by the shared C
    batch writer. Unlike :class:`MSZXManifest` it carries no ``created_at`` —
    the format is deliberately timestamp-free so identical inputs produce
    byte-identical archives.

    Every field beyond ``entry``/``original``/``size`` is optional, so archives
    written before those fields existed still parse.
    """

    version: str = "2.0"
    container: str = "batch"
    spectra_files: List[SpectraFileEntry] = field(default_factory=list)
    description: Optional[str] = None
    extra: Dict[str, Any] = field(default_factory=dict)

    # Highest batch manifest major version this binding can read. Unannotated
    # on purpose — see the note on MSZXManifest.MAX_SUPPORTED_MAJOR.
    MAX_SUPPORTED_MAJOR = 2

    def __len__(self) -> int:
        return len(self.spectra_files)

    def to_dict(self) -> Dict[str, Any]:
        data: Dict[str, Any] = {
            "version": self.version,
            "container": self.container,
        }
        if self.description:
            data["description"] = self.description
        if self.extra:
            data["extra"] = self.extra
        data["spectra_files"] = [e.to_dict() for e in self.spectra_files]
        return data

    def to_json(self, indent: int = 2) -> str:
        return json.dumps(self.to_dict(), indent=indent)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> MSZXBatchManifest:
        """Create from dictionary.

        Raises:
            ValueError: if the manifest is a newer major version than this
                build supports, or is not a batch manifest at all.
        """
        _reject_unsupported_major(data, cls.MAX_SUPPORTED_MAJOR)

        if not is_batch_manifest(data):
            raise ValueError(
                "not a multi-file (batch) MSZX manifest; use MSZXManifest for "
                "single-file archives"
            )

        return cls(
            version=str(data.get("version", "2.0")),
            container=str(data.get("container", "batch")),
            spectra_files=[
                SpectraFileEntry.from_dict(e) for e in data.get("spectra_files", [])
            ],
            description=data.get("description"),
            extra=data.get("extra", {}),
        )

    @classmethod
    def from_json(cls, json_str: str) -> MSZXBatchManifest:
        return cls.from_dict(json.loads(json_str))

    @classmethod
    def from_single_file(cls, manifest: MSZXManifest) -> MSZXBatchManifest:
        """Adapt a v1 single-file manifest into a one-member batch manifest.

        Lets one container type read every ``.mszx``: a v1 archive is simply a
        collection of one. ``container`` is reported as ``"single"`` and
        ``version`` is preserved, so callers can still tell the two apart.

        ``size`` is left at 0 — the v1 manifest does not record the payload
        length. :meth:`MSZXBatchFile.open` fills it in from the tar header.
        """
        return cls(
            version=manifest.version,
            container="single",
            spectra_files=[
                SpectraFileEntry(
                    entry=manifest.spectra_file,
                    original=manifest.source_file or "",
                    size=0,
                    num_spectra=manifest.num_spectra,
                    join_key=manifest.join_key,
                    annotations=list(manifest.annotations),
                )
            ],
            description=manifest.description,
            extra=dict(manifest.extra),
        )
