"""Re-chunk MSZ/MSZX files: rewrite a compressed file at a different ZSTD block
size without changing its data.

Recent dataloader memory work made the shared block cache budget-bounded, but
files written with large blocks suffer on random-access reads: fetching one
spectrum decompresses its entire ZSTD block. ``rechunk`` rewrites a file at a
smaller (or larger) block size, trading compression ratio against random-read
amplification.

The operation round-trips through mzML in a temporary directory and re-compresses
with the original lossy algorithms, scale factors, and stream formats preserved
-- only the block size changes. The MS lossy algorithms are precision-reducing
quantizers, so re-applying one to already-decoded data with the original scale
factor is idempotent (no extra precision is lost).

Use :func:`rechunk` for a format-agnostic entry point, or the ``rechunk`` method
on an open ``MSZFile`` / ``MSZXFile``.
"""

from __future__ import annotations

import os
import tempfile
from os import PathLike
from pathlib import Path
from typing import TYPE_CHECKING, Optional, Union

if TYPE_CHECKING:
    from mscompress._core import MSZFile, MSZXFile

__all__ = ["rechunk"]

# Decimal size suffixes, matching the C CLI's parse_blocksize (KB=1e3, MB=1e6,
# GB=1e9) so "8MB" means the same thing everywhere.
_SIZE_SUFFIXES = (("KB", 1_000), ("MB", 1_000_000), ("GB", 1_000_000_000))


def _parse_blocksize(value: Union[int, str]) -> int:
    """Coerce an int or a size string like ``"8MB"`` to a positive byte count."""
    if isinstance(value, bool):  # bool is an int subclass; reject explicitly
        raise TypeError("blocksize must be an int or size string, not bool")
    if isinstance(value, int):
        bs = value
    elif isinstance(value, str):
        s = value.strip().upper()
        mult = 1
        for suffix, factor in _SIZE_SUFFIXES:
            if s.endswith(suffix):
                mult, s = factor, s[: -len(suffix)].strip()
                break
        else:
            if s.endswith("B"):
                s = s[:-1].strip()
        try:
            bs = int(round(float(s) * mult))
        except ValueError:
            raise ValueError(f"Invalid blocksize: {value!r}") from None
    else:
        raise TypeError(f"blocksize must be an int or size string, got {type(value).__name__}")
    if bs <= 0:
        raise ValueError(f"blocksize must be positive, got {bs}")
    return bs


def _apply_config(arguments, cfg: dict) -> None:
    """Re-apply a source file's compression settings onto recompression args.

    The lossy setters reset the scale factor to the algorithm default, so the
    scale factors must be restored *after* the lossy algorithms are set to honor
    any custom factor the original file used.
    """
    arguments.mz_lossy = cfg["mz_lossy"]
    arguments.int_lossy = cfg["int_lossy"]
    arguments.target_xml_format = cfg["target_xml_format"]
    arguments.target_mz_format = cfg["target_mz_format"]
    arguments.target_inten_format = cfg["target_inten_format"]
    arguments.mz_scale_factor = cfg["mz_scale_factor"]
    arguments.int_scale_factor = cfg["int_scale_factor"]


def _recompress_mzml(mzml_path: Path, out_path: Path, cfg: dict, blocksize: int,
                     threads: Optional[int]):
    """Compress ``mzml_path`` to ``out_path`` using ``cfg`` and ``blocksize``."""
    from mscompress.utils import read

    mzml = read(str(mzml_path))
    try:
        _apply_config(mzml.arguments, cfg)
        mzml.arguments.blocksize = blocksize
        if threads is not None:
            mzml.arguments.threads = threads
        return mzml.compress(str(out_path))
    finally:
        mzml._cleanup()


def _finalize(produce, final_path: Path, in_place: bool, close_source) -> None:
    """Run ``produce(dest)`` then place the result at ``final_path``.

    For new-file output, ``produce`` writes straight to ``final_path``. For
    in-place output it writes to a sibling temp file, the source mapping is
    released via ``close_source()`` (so the file is replaceable on Windows), and
    the temp file atomically replaces the original.
    """
    if not in_place:
        final_path.parent.mkdir(parents=True, exist_ok=True)
        produce(final_path)
        return

    tmp_out = final_path.with_name(final_path.name + ".rechunk-tmp")
    try:
        produce(tmp_out)
        close_source()
        os.replace(str(tmp_out), str(final_path))
    finally:
        if tmp_out.exists():
            tmp_out.unlink()


def _rechunk_msz_file(msz, blocksize, output=None, threads=None):
    """Core implementation behind ``MSZFile.rechunk``."""
    from mscompress.utils import read

    blocksize = _parse_blocksize(blocksize)
    src_path = Path(msz.path.decode("utf-8"))
    cfg = msz._compression_config()
    in_place = output is None
    final_path = src_path if in_place else Path(os.fspath(output))

    with tempfile.TemporaryDirectory(prefix="mscompress-rechunk-") as td:
        tmp_mzml = Path(td) / (src_path.stem + ".mzML")
        msz.decompress(str(tmp_mzml))

        def produce(dest: Path):
            recompressed = _recompress_mzml(tmp_mzml, dest, cfg, blocksize, threads)
            recompressed._cleanup()

        _finalize(produce, final_path, in_place, msz._cleanup)

    return read(str(final_path))


def _rechunk_mszx_file(mszx, blocksize, output=None, threads=None):
    """Core implementation behind ``MSZXFile.rechunk``."""
    from mscompress._core import MSZFile, MSZXFile
    from mscompress.annotations import PSMReader
    from mscompress.mszx import MSZXBuilder

    blocksize = _parse_blocksize(blocksize)
    archive_path = Path(mszx.archive_path)
    cfg = mszx._compression_config()
    manifest = mszx.manifest
    annotations = mszx.annotation_readers or {}
    in_place = output is None
    final_path = archive_path if in_place else Path(os.fspath(output))
    if not final_path.suffix:
        final_path = final_path.with_suffix(".mszx")

    with tempfile.TemporaryDirectory(prefix="mscompress-rechunk-") as td:
        temp = Path(td)
        tmp_mzml = temp / (archive_path.stem + ".mzML")
        # MSZFile.decompress (not the MSZX override) writes just the embedded
        # mzML; annotations are rebuilt separately below.
        MSZFile.decompress(mszx, str(tmp_mzml))

        tmp_msz = temp / (archive_path.stem + ".msz")
        recompressed = _recompress_mzml(tmp_mzml, tmp_msz, cfg, blocksize, threads)
        try:
            builder = MSZXBuilder(recompressed, compression=True)
            if manifest is not None:
                if manifest.description:
                    builder.set_description(manifest.description)
                if manifest.join_key:
                    builder.set_join_key(manifest.join_key)
                for key, value in (manifest.extra or {}).items():
                    builder.set_extra(key, value)

            for filename, reader in annotations.items():
                out_name = filename[:-4] if filename.endswith(".zst") else filename
                ann_path = temp / out_name
                ann_path.write_bytes(reader.source.read())
                builder.add_annotations(PSMReader(ann_path))

            def produce(dest: Path):
                builder.save(dest)

            _finalize(produce, final_path, in_place, mszx._cleanup)
        finally:
            recompressed._cleanup()

    return MSZXFile.open(final_path)


def rechunk(
    input: Union[str, PathLike, bytes],
    blocksize: Union[int, str],
    output: Optional[Union[str, PathLike]] = None,
    *,
    threads: Optional[int] = None,
) -> "Union[MSZFile, MSZXFile]":
    """Rewrite an ``.msz`` or ``.mszx`` file at a different ZSTD block size.

    Smaller blocks reduce random-read amplification -- reading one spectrum
    decompresses its whole block -- at a modest compression-ratio cost. The data
    is preserved exactly: the file is round-tripped through mzML in a temporary
    directory and re-compressed with the original lossy algorithms, scale
    factors, and stream formats, changing only the block size.

    Args:
        input: Path to an existing ``.msz`` or ``.mszx`` file.
        blocksize: Target block size in bytes (int) or a size string such as
            ``"8MB"`` (decimal suffixes, matching the CLI).
        output: Destination path. ``None`` (default) rewrites ``input`` in place
            via an atomic replace. Otherwise a new file is written and ``input``
            is left untouched.
        threads: Optional worker-thread count for the re-compression.

    Returns:
        A freshly opened ``MSZFile`` or ``MSZXFile`` for the re-chunked output.
    """
    from mscompress.utils import read

    f = read(input)
    try:
        if not hasattr(f, "rechunk"):
            raise TypeError(
                "rechunk requires a compressed .msz or .mszx file; "
                f"got {type(f).__name__}"
            )
        return f.rechunk(blocksize, output=output, threads=threads)
    finally:
        # In-place rechunk already released f's mapping; _cleanup is idempotent.
        f._cleanup()
