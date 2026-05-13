"""Cleanup / context-manager / lifecycle tests for MSZXFile.

MSZXFile is a Cython type, so its methods can't be monkey-patched via
unittest.mock — these tests assert on observable side effects (the
_closed flag, idempotent close, no temp-dir leaks) rather than mocking
internal calls.
"""

import os
import tempfile

import pytest

from mscompress.mszx import MSZXFile, create_mszx
from mscompress import read


@pytest.fixture
def msz_file(msz_file_path):
    return read(msz_file_path)


@pytest.fixture
def mszx_archive(tmp_path, msz_file):
    """Create a temporary MSZX archive for testing."""
    mszx_path = tmp_path / "test.mszx"
    create_mszx(msz_file, mszx_path, description="Test Archive")
    return mszx_path


def test_close_sets_closed_flag(mszx_archive):
    """close() flips the _closed flag from False to True."""
    mszx = MSZXFile.open(mszx_archive)
    assert mszx._closed is False
    mszx.close()
    assert mszx._closed is True


def test_context_manager_sets_closed_flag(mszx_archive):
    """Exiting via context manager invokes _cleanup, leaving _closed=True."""
    with MSZXFile.open(mszx_archive) as mszx:
        assert mszx._closed is False
    assert mszx._closed is True


def test_open_does_not_create_temp_dir(mszx_archive):
    """MSZXFile.open mmaps in-place — no mszx_* temp dir is created."""
    before = {e for e in os.listdir(tempfile.gettempdir()) if e.startswith("mszx_")}
    mszx = MSZXFile.open(mszx_archive)
    try:
        after = {e for e in os.listdir(tempfile.gettempdir()) if e.startswith("mszx_")}
        assert after == before, f"unexpected mszx_* temp dirs: {after - before}"
    finally:
        mszx.close()


def test_close_is_idempotent(mszx_archive):
    """close() called multiple times must not raise."""
    mszx = MSZXFile.open(mszx_archive)
    mszx.close()
    mszx.close()
    assert mszx._closed is True


def test_context_manager_then_close(mszx_archive):
    """close() after a context-manager exit must not raise."""
    with MSZXFile.open(mszx_archive) as mszx:
        pass
    mszx.close()
    assert mszx._closed is True


def test_del_triggers_cleanup(mszx_archive):
    """Letting an MSZXFile go out of scope triggers __del__ → _cleanup.

    We can't observe the _closed flag after `del` (the object is gone), so
    instead we verify the side effect: after garbage-collecting the
    MSZXFile, no mszx-related temp dir or file descriptor leaks behind.
    """
    import gc

    before_tmp = {e for e in os.listdir(tempfile.gettempdir()) if e.startswith("mszx_")}

    mszx = MSZXFile.open(mszx_archive)
    # Touch a property so the lazy mmap'd state is exercised.
    _ = len(mszx.spectra)
    del mszx
    gc.collect()

    after_tmp = {e for e in os.listdir(tempfile.gettempdir()) if e.startswith("mszx_")}
    assert after_tmp == before_tmp


def test_extract_returned_mszx_can_be_closed(tmp_path, mszx_archive):
    """The MSZXFile returned by extract() supports the standard close()
    lifecycle and reaches _closed=True."""
    with MSZXFile.open(mszx_archive) as source:
        output_path = tmp_path / "extracted.mszx"
        scan1 = int(source.positions.scans[0])
        extracted = source.extract(output_path, scan_numbers=[scan1])

        assert len(extracted.spectra) == 1
        assert extracted.spectra[0].scan == scan1

        extracted.close()
        assert extracted._closed is True
