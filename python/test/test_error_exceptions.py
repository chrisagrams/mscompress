"""
Tests for fatal C ``error()`` -> Python exception mapping (GitHub issue #171).

The C core distinguishes two failure channels:

- ``error()`` — a genuine, fatal failure (e.g. a write failing on a broken
  pipe). This must surface as a catchable :class:`MSCompressError`, not a
  silently-swallowed warning.
- ``warning()`` — graceful degradation (e.g. corrupt base64 that yields an
  empty array by design, issue #111/#118). This must keep emitting a
  :class:`RuntimeWarning` and return normally.
"""

import os

import numpy as np
import pytest

from mscompress import MSCompressError, read


def test_mscompress_error_is_runtime_error():
    """MSCompressError stays a RuntimeError subclass for backward compatibility."""
    assert issubclass(MSCompressError, RuntimeError)


def test_decompress_to_broken_pipe_raises(msz_file_path):
    """A fatal C error() (write to a broken pipe) raises MSCompressError.

    Closing the read end of a pipe makes every write() to the write end fail
    with EPIPE, which the C ``write_to_file`` reports via ``error()``. That must
    propagate as an exception rather than being downgraded to a warning.
    """
    read_fd, write_fd = os.pipe()
    os.close(read_fd)  # break the pipe: subsequent writes fail with EPIPE
    try:
        with read(msz_file_path) as msz:
            with pytest.raises(MSCompressError):
                msz._decompress_to_fd(write_fd)
    finally:
        try:
            os.close(write_fd)
        except OSError:
            pass


def test_pending_error_does_not_leak_to_next_call(msz_file_path):
    """A fatal error from one operation must not poison a subsequent good one."""
    read_fd, write_fd = os.pipe()
    os.close(read_fd)
    try:
        with read(msz_file_path) as msz:
            with pytest.raises(MSCompressError):
                msz._decompress_to_fd(write_fd)
    finally:
        try:
            os.close(write_fd)
        except OSError:
            pass

    # A clean decompress afterwards must succeed (pending error was cleared).
    with read(msz_file_path) as msz:
        chunks = list(msz.decompress_stream())
        assert len(b"".join(chunks)) > 0


def test_warning_path_stays_graceful(corrupt_base64_mzml_path):
    """Corrupt base64 uses warning() — it must emit a RuntimeWarning and return
    an empty array, NOT raise MSCompressError."""
    mzml = read(corrupt_base64_mzml_path)
    with pytest.warns(RuntimeWarning):
        mz = mzml.get_mz_binary(2)
    assert isinstance(mz, np.ndarray)
    assert len(mz) == 0
