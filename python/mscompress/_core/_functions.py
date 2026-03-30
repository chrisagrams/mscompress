import cython
import os

from typing import Union

from cython.cimports.mscompress._core._bindings import (
    _get_num_threads,
    _get_filesize,
)


def get_num_threads() -> int:
    """
    Simple function to return current amount of threads on system.

    Returns:
    int: Number of usable threads.
    """
    return _get_num_threads()


def get_filesize(path: Union[str, bytes]) -> int:
    """
    Simple function to get filesize of file.

    Parameters:
    path (Union[str, bytes]): Path to file. Can be a string or bytes.

    Returns:
    int: Size of the file in bytes.
    """
    if isinstance(path, str):
        path = path.encode('utf-8')

    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")

    return _get_filesize(path)
