"""Rewrite an existing .mszx so its search results are stored as mzTab.

Archives written before annotations were unified on mzTab carry the search
engine's own files (pepXML, Percolator TSV, ...). :func:`upgrade_mszx`
converts each of those members to mzTab, drops the original, and updates the
manifest. Spectra payloads and any other members are copied byte-for-byte.
Works on both v1 (single-file) and v2 (batch) archives.

Command line::

    python -m mscompress.mszx.upgrade in.mszx [-o out.mszx]
"""

from __future__ import annotations

import argparse
import io
import json
import os
import tarfile
import tempfile
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

import zstandard as zstd

from mscompress.annotations import (
    MSZXAnnotationFile,
    PSMReader,
    mztab_member_name,
    reader_to_mztab,
)
from mscompress.annotations._base import DEFAULT_ZSTD_COMPRESSION_LEVEL
from mscompress.mszx.metadata import is_batch_manifest
from mscompress.types import AnnotationFormat

__all__ = ["upgrade_mszx"]


def _annotation_groups(manifest: Dict[str, Any]) -> List[tuple[str, List[Dict[str, Any]]]]:
    """(ms_run location, annotation entry dicts) for every spectra file.

    Batch entries use the source mzML name, matching what
    ``MSZXBatchWriter.add_psms`` records; v1 archives use the MSZ member.
    """
    if is_batch_manifest(manifest):
        return [
            (entry.get("original") or entry["entry"], entry.get("annotations", []))
            for entry in manifest.get("spectra_files", [])
        ]
    return [(manifest.get("spectra_file", "spectra.msz"), manifest.get("annotations", []))]


def upgrade_mszx(
    path: Union[str, Path],
    output: Optional[Union[str, Path]] = None,
) -> Path:
    """
    Convert every non-mzTab annotation member of an archive to mzTab.

    Args:
        path: Archive to upgrade.
        output: Destination. Defaults to rewriting `path` in place (via a
            temporary file that replaces it only once fully written).

    Returns:
        Path to the upgraded archive.

    Raises:
        ValueError: if the archive has no manifest, or an annotation member
            cannot be converted.
    """
    src = Path(path)
    dst = Path(output) if output is not None else src

    with tarfile.open(src, "r") as tar:
        try:
            manifest_member = tar.getmember("manifest.json")
        except KeyError:
            raise ValueError("Invalid MSZX archive: missing manifest.json") from None
        handle = tar.extractfile(manifest_member)
        if handle is None:
            raise ValueError("Could not read manifest.json")
        manifest = json.loads(handle.read().decode("utf-8"))

        # Convert first, so a failure leaves nothing half-written.
        replacements: Dict[str, Optional[tuple[str, bytes]]] = {}
        taken = {m.name for m in tar.getmembers()}
        for location, entries in _annotation_groups(manifest):
            for entry in entries:
                if entry.get("format") == AnnotationFormat.MZTAB.value:
                    continue
                old_name = entry["filename"]
                compressed = bool(entry.get("compressed", False))
                try:
                    source = MSZXAnnotationFile(tar, old_name, name=old_name)
                    reader = PSMReader(source)
                    data = reader_to_mztab(
                        reader, ms_run_location=Path(location).name
                    ).encode("utf-8")
                except Exception as exc:
                    raise ValueError(
                        f"Could not convert annotation {old_name!r} to mzTab: {exc}"
                    ) from exc
                if compressed:
                    data = zstd.ZstdCompressor(
                        level=DEFAULT_ZSTD_COMPRESSION_LEVEL
                    ).compress(data)

                # Keep any directory prefix the member had.
                parent = os.path.dirname(old_name)
                new_name = mztab_member_name(old_name, compressed)
                if parent:
                    new_name = f"{parent}/{new_name}"
                if new_name in taken and new_name != old_name:
                    raise ValueError(f"Cannot rename {old_name!r}: {new_name!r} exists")
                taken.add(new_name)

                replacements[old_name] = (new_name, data)
                entry.update(
                    filename=new_name,
                    format=AnnotationFormat.MZTAB.value,
                    compressed=compressed,
                    num_records=len(reader),
                )

        if not replacements:
            if dst != src:
                dst.write_bytes(src.read_bytes())
            return dst

        if is_batch_manifest(manifest):
            for entry in manifest.get("spectra_files", []):
                entry["join_key"] = "spectra_ref"
        else:
            manifest["join_key"] = "spectra_ref"
        manifest_bytes = json.dumps(manifest, indent=2).encode("utf-8")

        fd, tmp_name = tempfile.mkstemp(dir=dst.parent, suffix=".mszx.tmp")
        os.close(fd)
        try:
            with tarfile.open(tmp_name, "w") as out:
                for member in tar.getmembers():
                    if member.name == "manifest.json":
                        info = tarfile.TarInfo("manifest.json")
                        info.size = len(manifest_bytes)
                        info.mtime = member.mtime
                        out.addfile(info, io.BytesIO(manifest_bytes))
                    elif member.name in replacements:
                        new_name, data = replacements[member.name]
                        info = tarfile.TarInfo(new_name)
                        info.size = len(data)
                        info.mtime = member.mtime
                        out.addfile(info, io.BytesIO(data))
                    elif member.isfile():
                        out.addfile(member, tar.extractfile(member))
                    else:
                        out.addfile(member)
            os.replace(tmp_name, dst)
        except BaseException:
            if os.path.exists(tmp_name):
                os.unlink(tmp_name)
            raise

    return dst


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m mscompress.mszx.upgrade",
        description="Convert an .mszx archive's search results to mzTab.",
    )
    parser.add_argument("input", help="archive to upgrade")
    parser.add_argument(
        "-o", "--output", help="write here instead of rewriting INPUT in place"
    )
    args = parser.parse_args(argv)
    print(upgrade_mszx(args.input, args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
