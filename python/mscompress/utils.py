import os
from os import PathLike
import tarfile
from pathlib import Path
from typing import Literal, Optional
from mscompress._core import MZMLFile, MSZFile, CACHE_SPECTRA_AUTO
from mscompress.mszx import MSZXFile
from typing import Union


def read(
    path: Union[str, PathLike, bytes],
    cache=None,
    *,
    cache_spectra: int = CACHE_SPECTRA_AUTO,
) -> Union[MZMLFile, MSZFile, MSZXFile]:
    """
    Read and parse mzML, MSZ, or MSZX files.

    Args:
        path (Union[str, PathLike, bytes]): Path to the file to read.
        cache: Shared decompressed-block cache for MSZ/MSZX files. ``None``
            (default) uses the process-wide default ``BlockCache``; pass a
            ``BlockCache`` to share an explicit pool across files, an int to set
            a private byte budget, or 0 to disable bounding (legacy unbounded).
            Ignored for mzML inputs.
        cache_spectra (int): Per-file LRU cap for cached ``Spectrum`` objects.
            ``CACHE_SPECTRA_AUTO`` (-1, default) uses the bundled default;
            ``0`` disables eviction (legacy unbounded); ``N > 0`` sets an
            explicit cap.
    Returns:
        Union[MZMLFile, MSZFile, MSZXFile]: Parsed file object.
    """

    if not isinstance(path, (str, bytes, PathLike)):
        raise TypeError("Path must be a string, bytes, or PathLike.")
    
    # Handle bytes first, then use os.fspath() for PathLike objects (PEP 519)
    if isinstance(path, bytes):
        path_str = path.decode('utf-8')
    else:
        path_str = os.fspath(path)
    
    path_str = os.path.expanduser(path_str)
    path_str = os.path.abspath(path_str)

    if not os.path.exists(path_str):
        raise FileNotFoundError(f"The specified file does not exist: {path_str}")
    
    if os.path.isdir(path_str):
        raise IsADirectoryError(f"The specified path is a directory, not a file: {path_str}")
    
    filetype = detect_filetype(str(path_str))
    
    # Convert to bytes for C extension functions
    path_bytes = path_str.encode('utf-8')

    if filetype == "mzML":
        f = MZMLFile(path_bytes)
    elif filetype == "msz":
        f = MSZFile(path_bytes, cache=cache)
    elif filetype == "mszx":
        f = MSZXFile.open(path_str, cache=cache)
    else:
        raise OSError(f"Could not determine file type for: {path_str}")

    # Set before the first `.spectra` access so the Spectra LRU is built with
    # this cap. The file constructors deliberately don't expose this knob;
    # read() is the single entry point for it.
    f._cache_spectra = cache_spectra
    return f


def detect_filetype(path: Union[str, Path]) -> Optional[Literal["mzML", "msz", "mszx"]]:
    """
    Determine the file type by examining file contents.
    
    Args:
        path: Path to the file to check
        
    Returns:
        "mzML" if the file is an mzML file (contains "indexedmzML" in first 512 bytes)
        "msz" if the file is an msz file (starts with magic tag 0x035F51B5)
        "mszx" if the file is a tar archive containing manifest.json
        None if the file type cannot be determined
    """
    path = Path(path)
    
    if not path.exists() or not path.is_file():
        return None
    
    try:
        # Read first 512 bytes
        with open(path, 'rb') as f:
            header = f.read(512)
        
        if len(header) == 0:
            return None
        
        # Check for msz magic tag (0x035F51B5) at the beginning
        if len(header) >= 4:
            magic = int.from_bytes(header[:4], byteorder='little')
            if magic == 0x035F51B5:
                return "msz"
        
        # Check for mzML (contains "indexedmzML" string)
        if b"indexedmzML" in header:
            return "mzML"
        
        # Check for mszx (tar file with manifest.json)
        try:
            if tarfile.is_tarfile(path):
                with tarfile.open(path, 'r') as tar:
                    members = tar.getnames()
                    if "manifest.json" in members:
                        return "mszx"
        except (tarfile.TarError, OSError):
            pass
        
        return None
        
    except (IOError, OSError):
        return None
