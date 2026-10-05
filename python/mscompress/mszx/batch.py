"""Collection reader and writer for MSZX archives.

A v2 ("batch") archive bundles N independent MSZ files, so — unlike the
single-file :class:`~mscompress.MSZXFile`, which *is* an ``MSZFile`` — the
container cannot itself be an ``MSZFile``. :class:`MSZXBatchFile` is a
collection of them.

It also opens a **v1** archive, as a collection of one. That makes it the
single reader that handles every ``.mszx`` uniformly; ``MSZXFile`` remains the
flat, is-a-``MSZFile`` reader when you specifically want v1's spectrum API.

Members are opened lazily via ``MSZFile.from_mszx()``, which mmaps the embedded
MSZ at its byte offset inside the tar. Nothing is extracted to a temp directory,
and a 1000-member archive does not consume 1000 file descriptors unless every
member is actually touched.
"""

from __future__ import annotations

import glob
import json
import os
import tarfile
import warnings
from pathlib import Path
from typing import Any, Dict, Iterable, Iterator, List, Optional, Union

from mscompress._core import MSZFile  # ty: ignore[unresolved-import]
from mscompress.mszx.metadata import (
    MSZXBatchManifest,
    MSZXManifest,
    SpectraFileEntry,
    is_batch_manifest,
)

__all__ = [
    "MSZXBatchFile",
    "compress_batch",
    "read_archive_manifest",
    "resolve_mzml_inputs",
]


def read_archive_manifest(path: Union[str, Path]) -> MSZXBatchManifest:
    """Read ``manifest.json`` out of any ``.mszx``, as a batch manifest.

    A v2 archive parses directly. A **v1** single-file archive is adapted into
    a one-member batch manifest, so this container can read either version —
    handy when walking a directory of mixed archives without branching.

    Only the manifest member is read; the MSZ payloads stay on disk and are
    mmap'd on demand.
    """
    with tarfile.open(path, "r") as tar:
        try:
            member = tar.getmember("manifest.json")
        except KeyError:
            raise ValueError("Invalid MSZX archive: missing manifest.json") from None
        handle = tar.extractfile(member)
        if handle is None:
            raise ValueError("Could not read manifest.json")
        data = json.loads(handle.read().decode("utf-8"))

        if is_batch_manifest(data):
            return MSZXBatchManifest.from_dict(data)

        # v1: adapt to a one-member collection, and recover the payload size
        # the single-file manifest never recorded.
        adapted = MSZXBatchManifest.from_single_file(MSZXManifest.from_dict(data))
        try:
            adapted.spectra_files[0].size = tar.getmember(
                adapted.spectra_files[0].entry
            ).size
        except KeyError:
            pass  # missing payload surfaces on first access, not here
        return adapted


class MSZXBatchFile:
    """An MSZX archive viewed as a collection of MSZ files.

    Holds N members for a v2 ("batch") archive and exactly one for a v1
    archive, so the same code handles either. Behaves like an ordered,
    lazily-populated collection of :class:`MSZFile`:

    .. code-block:: python

        with mscompress.read("cohort.mszx") as archive:
            print(len(archive), archive.names)
            print(archive.entries[0].num_spectra)   # from the manifest, no open
            for member in archive:                  # opened on demand
                print(len(member.spectra))
            first = archive["a.msz"]                # or archive[0]

    Members are cached once opened and released together by :meth:`close`.
    """

    def __init__(self, path: Union[str, Path], manifest: MSZXBatchManifest):
        self._archive_path = Path(path)
        self._manifest = manifest
        self._open_members: Dict[str, MSZFile] = {}
        self._closed = False

    @classmethod
    def open(cls, path: Union[str, Path]) -> MSZXBatchFile:
        """Open any ``.mszx`` for reading as a collection.

        A v2 batch archive yields N members; a **v1** single-file archive
        yields exactly one. Use this when you want to handle either version
        uniformly. (:class:`~mscompress.MSZXFile` remains the flat, is-a-MSZFile
        reader for v1.)

        Raises:
            FileNotFoundError: if `path` does not exist.
            ValueError: if the archive has no manifest.
        """
        archive_path = Path(path)
        if not archive_path.exists():
            raise FileNotFoundError(f"MSZX file not found: {archive_path}")
        return cls(archive_path, read_archive_manifest(archive_path))

    # -- metadata (no member is opened) ------------------------------------

    @property
    def manifest(self) -> MSZXBatchManifest:
        """The archive manifest."""
        return self._manifest

    @property
    def archive_path(self) -> Path:
        """Path to the .mszx archive."""
        return self._archive_path

    @property
    def entries(self) -> List[SpectraFileEntry]:
        """Manifest records, including ``num_spectra`` where the writer set it."""
        return self._manifest.spectra_files

    @property
    def names(self) -> List[str]:
        """Tar member names of the MSZ payloads, in archive order."""
        return [e.entry for e in self._manifest.spectra_files]

    def describe(self) -> Dict[str, Any]:
        """Summary of the archive without opening any member."""
        return {
            "path": str(self._archive_path),
            "container": self._manifest.container,
            "version": self._manifest.version,
            "n_entries": len(self._manifest.spectra_files),
            "description": self._manifest.description,
            "entries": [e.to_dict() for e in self._manifest.spectra_files],
        }

    # -- member access -----------------------------------------------------

    def _entry_for(self, key: Union[int, str]) -> SpectraFileEntry:
        files = self._manifest.spectra_files
        if isinstance(key, int):
            try:
                return files[key]
            except IndexError:
                raise IndexError(
                    f"entry index {key} out of range ({len(files)} entries)"
                ) from None
        for entry in files:
            if entry.entry == key:
                return entry
        raise KeyError(f"No such entry in archive: {key!r}")

    def get(self, key: Union[int, str]) -> MSZFile:
        """Open a member by index or tar member name.

        The returned :class:`MSZFile` is owned by this archive — it is cached
        and closed by :meth:`close`. Do not close it yourself.
        """
        self._check_open()
        entry = self._entry_for(key)
        member = self._open_members.get(entry.entry)
        if member is None:
            member = MSZFile.from_mszx(
                str(self._archive_path).encode(), entry.entry.encode()
            )
            self._open_members[entry.entry] = member
        return member

    def __getitem__(self, key: Union[int, str]) -> MSZFile:
        return self.get(key)

    def __len__(self) -> int:
        return len(self._manifest.spectra_files)

    def __iter__(self) -> Iterator[MSZFile]:
        for i in range(len(self)):
            yield self.get(i)

    def __contains__(self, name: object) -> bool:
        return any(e.entry == name for e in self._manifest.spectra_files)

    @property
    def spectra(self):
        """Not available on a collection — pick a member first."""
        raise AttributeError(
            f"{type(self).__name__} holds {len(self)} MSZ file(s) and has no "
            f"single .spectra; use archive[0].spectra (or iterate the archive)"
        )

    # -- annotations -------------------------------------------------------

    def get_annotation(self, key: Union[int, str], filename: str) -> bytes:
        """Raw bytes of an annotation member attached to a spectra entry.

        Payloads flagged ``compressed`` in the manifest are returned as stored;
        the caller decompresses (the writer does not compress on your behalf).
        """
        entry = self._entry_for(key)
        for ann in entry.annotations:
            if ann.filename == filename:
                break
        else:
            raise KeyError(
                f"Annotation {filename!r} is not attached to entry "
                f"{entry.entry!r}"
            )
        with tarfile.open(self._archive_path, "r") as tar:
            try:
                member = tar.getmember(filename)
            except KeyError:
                raise KeyError(
                    f"Annotation file {filename!r} listed in the manifest is "
                    f"missing from the archive"
                ) from None
            handle = tar.extractfile(member)
            if handle is None:
                raise ValueError(f"Could not read annotation {filename!r}")
            return handle.read()

    # -- bulk output -------------------------------------------------------

    def decompress(self, output_dir: Union[str, Path]) -> List[Path]:
        """Expand every member to ``<output_dir>/<entry stem>.mzML``.

        Returns the written paths, in archive order.
        """
        self._check_open()
        out = Path(output_dir)
        out.mkdir(parents=True, exist_ok=True)

        written: List[Path] = []
        for entry in self._manifest.spectra_files:
            stem = entry.entry[:-4] if entry.entry.endswith(".msz") else entry.entry
            target = out / f"{stem}.mzML"
            self.get(entry.entry).decompress(str(target))
            written.append(target)
        return written

    # -- lifecycle ---------------------------------------------------------

    def _check_open(self) -> None:
        if self._closed:
            raise ValueError("Operation on a closed MSZXBatchFile")

    def close(self) -> None:
        """Close every opened member and release the archive. Idempotent."""
        for member in self._open_members.values():
            try:
                # MSZFile releases its fd + mapping through _cleanup(); only
                # MSZXFile exposes a public close().
                closer = getattr(member, "close", None) or member._cleanup
                closer()
            except Exception as exc:  # pragma: no cover - defensive
                warnings.warn(f"Error closing archive member: {exc}", stacklevel=2)
        self._open_members.clear()
        self._closed = True

    @property
    def closed(self) -> bool:
        return self._closed

    def __enter__(self) -> MSZXBatchFile:
        return self

    def __exit__(self, *exc_info: object) -> None:
        self.close()

    def __reduce__(self):
        # Re-open from the archive path so the container survives the
        # multiprocessing DataLoader path. Open members are not carried over;
        # the child re-opens them lazily.
        return (MSZXBatchFile.open, (str(self._archive_path),))

    def __repr__(self) -> str:
        state = "closed" if self._closed else f"{len(self)} entries"
        return f"<MSZXBatchFile {self._archive_path.name!r} ({state})>"


def resolve_mzml_inputs(
    inputs: Union[str, Path, Iterable[Union[str, Path]]],
    recursive: bool = False,
) -> List[Path]:
    """Expand paths, directories and globs into a sorted list of mzML files.

    Idiomatic Python resolution (``pathlib``/``glob``) rather than a port of
    the CLI's walker — but the ordering matches it: sorted by basename, then by
    full path, so archives built from the same set are deterministic.

    Directories contribute only ``*.mzML`` (case-insensitive). Explicitly named
    files are taken as-is, matching the CLI, so a deliberately odd extension
    still works.
    """
    if isinstance(inputs, (str, Path)):
        inputs = [inputs]

    found: List[Path] = []
    for token in inputs:
        text = os.fspath(token)
        path = Path(text)
        if path.is_dir():
            pattern = "**/*" if recursive else "*"
            found.extend(
                p for p in path.glob(pattern)
                if p.is_file() and p.suffix.lower() == ".mzml"
            )
        elif any(ch in text for ch in "*?["):
            found.extend(Path(p) for p in sorted(glob.glob(text, recursive=recursive)))
        elif path.is_file():
            found.append(path)
        else:
            warnings.warn(f"No input matched: {text}", UserWarning, stacklevel=2)

    # Deduplicate exact paths, then order by (basename, full path) as the CLI does.
    unique = {str(p): p for p in found}
    return sorted(unique.values(), key=lambda p: (p.name, str(p)))


def compress_batch(
    inputs: Union[str, Path, Iterable[Union[str, Path]]],
    output: Union[str, Path],
    *,
    recursive: bool = False,
    description: Optional[str] = None,
    extra: Optional[Dict[str, Any]] = None,
    on_progress: Optional[Any] = None,
    **compress_args: Any,
) -> Path:
    """Compress many mzML into one v2 .mszx archive.

    Args:
        inputs: A path, directory, glob, or iterable of any of those.
        output: Destination .mszx. Must be a seekable regular file.
        recursive: Descend into subdirectories for directory inputs.
        description: Archive-level free-text description.
        extra: Archive-level metadata dict, embedded in the manifest.
        on_progress: Optional ``callable(index, total, path)`` invoked before
            each entry is compressed.
        **compress_args: Forwarded to :class:`RuntimeArguments` (``threads``,
            ``blocksize``, ``mz_lossy``, ``int_lossy``, ...).

    Returns:
        Path to the written archive.

    Raises:
        ValueError: if no input matched, or an input could not be compressed.

    Example:
        >>> compress_batch("runs/", "cohort.mszx", recursive=True, threads=8)
    """
    from mscompress._core import MSZXBatchWriter  # ty: ignore[unresolved-import]

    files = resolve_mzml_inputs(inputs, recursive=recursive)
    if not files:
        raise ValueError("No input mzML files matched; nothing to do.")

    out_path = Path(output)
    writer = MSZXBatchWriter(out_path, **compress_args)
    try:
        if description is not None:
            writer.set_description(description)
        if extra is not None:
            writer.set_extra(extra)
        for i, path in enumerate(files):
            if on_progress is not None:
                on_progress(i, len(files), path)
            writer.add(path)
        writer.finish()
    except BaseException:
        # Leave no partial archive behind, including on KeyboardInterrupt.
        writer.abort()
        raise
    return out_path
