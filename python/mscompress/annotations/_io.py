"""Loading search results from paths or bytes, with transparent zstd."""

from __future__ import annotations

from os import PathLike, fspath
from pathlib import Path
from typing import Optional, Tuple, Union

import zstandard as zstd

DEFAULT_ZSTD_COMPRESSION_LEVEL = 3

_ZSTD_MAGIC = b"\x28\xb5\x2f\xfd"

Source = Union[str, PathLike, bytes, bytearray, memoryview]


def load(source: Source) -> Tuple[bytes, Optional[str]]:
    """
    Read a source into memory.

    Args:
        source: Path to a file, or the file's contents. zstd-compressed data
            is decompressed.

    Returns:
        The (decompressed) bytes and the file name, without any ``.zst``
        suffix (``None`` for in-memory data).
    """
    if isinstance(source, (bytes, bytearray, memoryview)):
        data, name = bytes(source), None
    else:
        path = Path(fspath(source))
        data, name = path.read_bytes(), path.name
    if data[:4] == _ZSTD_MAGIC:
        data = zstd.ZstdDecompressor().decompressobj().decompress(data)
    if name is not None and name.lower().endswith(".zst"):
        name = name[:-4]
    return data, name


def compress(data: bytes, level: int = DEFAULT_ZSTD_COMPRESSION_LEVEL) -> bytes:
    """zstd-compress data."""
    return zstd.ZstdCompressor(level=level).compress(data)
