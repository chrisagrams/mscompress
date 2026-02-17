import pytest
from pathlib import Path
from mscompress.mszx import MSZXFile, create_mszx
from mscompress import read


class CleanupTracker:
    """Wrapper that delegates to a real MSZFile but tracks _cleanup() calls."""
    def __init__(self, real_msz):
        object.__setattr__(self, '_real', real_msz)
        object.__setattr__(self, 'cleanup_count', 0)

    def _cleanup(self):
        object.__setattr__(self, 'cleanup_count', self.cleanup_count + 1)
        self._real._cleanup()

    def __getattr__(self, name):
        return getattr(self._real, name)


@pytest.fixture
def msz_file(msz_file_path):
    return read(msz_file_path)


@pytest.fixture
def mszx_archive(tmp_path, msz_file):
    """Create a temporary MSZX archive for testing."""
    mszx_path = tmp_path / "test.mszx"
    create_mszx(msz_file, mszx_path, description="Test Archive")
    return mszx_path


def test_close_calls_msz_cleanup(mszx_archive):
    """close() should call _cleanup() on the underlying MSZFile."""
    mszx = MSZXFile.open(mszx_archive)
    tracker = CleanupTracker(mszx.msz)
    mszx.msz = tracker

    mszx.close()

    assert tracker.cleanup_count == 1, "_cleanup() was not called on MSZFile"


def test_context_manager_calls_msz_cleanup(mszx_archive):
    """__exit__ (via context manager) should call _cleanup() on the underlying MSZFile."""
    with MSZXFile.open(mszx_archive) as mszx:
        tracker = CleanupTracker(mszx.msz)
        mszx.msz = tracker

    assert tracker.cleanup_count == 1, "_cleanup() was not called via context manager"


def test_close_sets_closed_flag(mszx_archive):
    """close() should set the _closed flag."""
    mszx = MSZXFile.open(mszx_archive)
    assert not mszx._closed
    mszx.close()
    assert mszx._closed


def test_close_removes_temp_dir(mszx_archive):
    """close() should remove the temporary directory."""
    mszx = MSZXFile.open(mszx_archive)
    temp_dir = mszx._temp_dir
    assert Path(temp_dir).exists()
    mszx.close()
    assert not Path(temp_dir).exists()


def test_close_is_idempotent(mszx_archive):
    """Calling close() multiple times should not raise."""
    mszx = MSZXFile.open(mszx_archive)
    mszx.close()
    mszx.close()  # Should not raise


def test_close_calls_cleanup_only_once(mszx_archive):
    """close() called twice should only invoke _cleanup() once."""
    mszx = MSZXFile.open(mszx_archive)
    tracker = CleanupTracker(mszx.msz)
    mszx.msz = tracker

    mszx.close()
    mszx.close()

    assert tracker.cleanup_count == 1, "_cleanup() should only be called once"


def test_context_manager_then_close(mszx_archive):
    """Exiting context then calling close() explicitly should not raise."""
    with MSZXFile.open(mszx_archive) as mszx:
        pass
    mszx.close()  # Should not raise


def test_del_calls_close(mszx_archive):
    """__del__ should trigger close() and clean up the MSZFile."""
    mszx = MSZXFile.open(mszx_archive)
    temp_dir = mszx._temp_dir
    tracker = CleanupTracker(mszx.msz)
    mszx.msz = tracker

    del mszx  # Triggers __del__ -> close() -> msz._cleanup()

    assert tracker.cleanup_count == 1
    assert not Path(temp_dir).exists()


def test_extract_returned_mszx_cleans_up_on_close(tmp_path, mszx_archive):
    """The MSZXFile returned by extract() should clean up its MSZFile on close."""
    with MSZXFile.open(mszx_archive) as source:
        output_path = tmp_path / "extracted.mszx"
        scans = source.msz.positions.scans
        scan1 = int(scans[0])

        extracted = source.extract(output_path, scan_numbers=[scan1])

        # Verify functional before close
        assert len(extracted.spectra) == 1
        assert extracted.spectra[0].scan == scan1

        # Now install tracker and close
        tracker = CleanupTracker(extracted.msz)
        extracted.msz = tracker
        extracted.close()

        assert tracker.cleanup_count == 1
