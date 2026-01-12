"""Base classes for annotation file handling with compression."""

from __future__ import annotations

import tarfile
from abc import ABC, abstractmethod
from pathlib import Path
from typing import (
    Optional,
    Union,
)

import zstandard as zstd

# Default compression level
DEFAULT_ZSTD_COMPRESSION_LEVEL = 3

class BaseAnnotationFile(ABC):
    """Abstract base class for annotation file sources."""
    
    def __init__(self):
        """Initialize the base annotation file."""
        self._cached_data: Optional[bytes] = None
    
    @property
    @abstractmethod
    def name(self) -> Optional[str]:
        """Get the name of this source."""
        pass
    
    @property
    def path(self) -> Optional[Path]:
        """Get the file path if this source is from a file."""
        return None
    
    @abstractmethod
    def read(self) -> bytes:
        """
        Read the data from this source.

        Returns:
            The data bytes (decompressed).
        """
        pass
    
    def get_compressed(self, level: int = DEFAULT_ZSTD_COMPRESSION_LEVEL) -> bytes:
        """
        Get compressed data from this source.
        
        Args:
            level: Zstd compression level (default: 3).
        
        Returns:
            The compressed data bytes.
        """
        data = self.read()
        cctx = zstd.ZstdCompressor(level=level)
        return cctx.compress(data)
    

class PathAnnotationFile(BaseAnnotationFile):
    """Annotation file source from a file on disk."""
    
    def __init__(self, file_path: Union[str, Path]):
        """
        Initialize from a file path.
        
        Args:
            file_path: Path to an annotation file on disk.
        """
        super().__init__()
        self._file_path = Path(file_path)
    
    @property
    def name(self) -> Optional[str]:
        """Get the filename."""
        return self._file_path.name
    
    @property
    def path(self) -> Optional[Path]:
        """Get the file path."""
        return self._file_path
    
    def read(self) -> bytes:
        """Read the data from the file."""
        if self._cached_data is not None:
            return self._cached_data
        
        with self._file_path.open("rb") as f:
            raw_data = f.read()
        
        try:
            dctx = zstd.ZstdDecompressor()
            self._cached_data = dctx.decompress(raw_data)
        except zstd.ZstdError:
            # Not compressed
            self._cached_data = raw_data
        
        return self._cached_data


class MSZXAnnotationFile(BaseAnnotationFile):
    """Annotation file source from a MSZX archive"""
    
    def __init__(
        self,
        mszx_file: tarfile.TarFile,
        member: Union[str, tarfile.TarInfo],
        name: Optional[str] = None,
    ):
        """
        Initialize from a MSZX archive member.
        
        Args:
            mszx_file: An open TarFile to read from.
            member: Member name or TarInfo within the tar file.
            name: Optional name override for the source.
        """
        super().__init__()
        self._name = name
        
        # Eagerly read and cache data while tar file is open
        if isinstance(member, str):
            member = mszx_file.getmember(member)
        
        if self._name is None:
            self._name = member.name
            
        extracted = mszx_file.extractfile(member)
        if extracted is None:
            raise ValueError(f"Could not extract {member.name} from MSZX")
        raw_data = extracted.read()
        
        # Decompress if needed
        try:
            dctx = zstd.ZstdDecompressor()
            self._cached_data = dctx.decompress(raw_data)
        except zstd.ZstdError:
            # Not compressed
            self._cached_data = raw_data
    
    @property
    def name(self) -> Optional[str]:
        """Get the member name."""
        return self._name
    
    def read(self) -> bytes:
        """Read the cached data from the MSZX member."""
        if self._cached_data is None:
            raise ValueError("Data was not cached during initialization")
        return self._cached_data