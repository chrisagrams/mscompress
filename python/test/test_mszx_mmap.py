"""Tests for the mmap-into-tar MSZX open path.

The MSZX loader opens the embedded MSZ via MSZFile.from_mszx, which mmaps
the .mszx archive at the entry's byte offset rather than extracting the
MSZ to a temp file. These tests verify:

  - no temp directory is created on open;
  - data read through the mmap'd MSZ is byte-identical to data read from
    the same MSZ extracted to a standalone file;
  - MSZFile instances opened from an archive cannot be pickled (the path
    no longer points to a standalone MSZ).
"""

import os
import pickle
import shutil
import tarfile
import tempfile
from pathlib import Path

import pytest

from mscompress import MSZFile
from mscompress.mszx import MSZXFile


@pytest.fixture
def standalone_msz_path(mszx_file_path, tmp_path):
    """Extract the MSZ entry from the test MSZX into a standalone .msz file."""
    with tarfile.open(mszx_file_path, "r") as tar:
        msz_member = next(m for m in tar.getmembers() if m.name.endswith(".msz"))
        tar.extract(msz_member, tmp_path)
    return tmp_path / msz_member.name


def test_no_temp_dir_created(mszx_file_path):
    """MSZXFile.open must not leave any mszx_* directory in $TMPDIR."""
    tmpdir = tempfile.gettempdir()
    before = {e for e in os.listdir(tmpdir) if e.startswith("mszx_")}

    mszx = MSZXFile.open(mszx_file_path)
    try:
        assert mszx._temp_dir is None
        after = {e for e in os.listdir(tmpdir) if e.startswith("mszx_")}
        assert after == before, f"new mszx_* entries appeared: {after - before}"
    finally:
        mszx.close()


def test_msz_data_integrity_via_mmap(mszx_file_path, standalone_msz_path):
    """Spectra read through the mmap'd MSZ must match the standalone MSZ
    byte-for-byte."""
    direct = MSZFile(str(standalone_msz_path).encode())
    via_archive = MSZXFile.open(mszx_file_path)
    try:
        assert len(via_archive.spectra) == len(direct.spectra)

        # Compare scan numbers.
        direct_scans = [int(s) for s in direct.positions.scans]
        archive_scans = [int(s) for s in via_archive.msz.positions.scans]
        assert direct_scans == archive_scans

        # Sample a few spectra and byte-compare m/z + intensity arrays.
        sample_indices = [0, len(direct.spectra) // 2, len(direct.spectra) - 1]
        for i in sample_indices:
            d_mz = direct.get_mz_binary(i)
            a_mz = via_archive.get_mz_binary(i)
            assert d_mz.tobytes() == a_mz.tobytes(), f"mz mismatch at index {i}"

            d_int = direct.get_inten_binary(i)
            a_int = via_archive.get_inten_binary(i)
            assert d_int.tobytes() == a_int.tobytes(), f"intensity mismatch at index {i}"
    finally:
        direct._cleanup()
        via_archive.close()


def test_pickle_raises_on_archive_opened(mszx_file_path):
    """An MSZFile opened from an MSZX archive cannot be pickled — the path
    points to the .mszx, not to a standalone MSZ, so naive re-open would
    misinterpret the file."""
    mszx = MSZXFile.open(mszx_file_path)
    try:
        with pytest.raises(TypeError, match="cannot be pickled"):
            pickle.dumps(mszx.msz)
    finally:
        mszx.close()


def test_decompress_to_directory(mszx_file_path, tmp_path):
    """MSZXFile.decompress(<dir>) must write the mzML and any annotation
    files into the directory."""
    out_dir = tmp_path / "decompressed"
    with MSZXFile.open(mszx_file_path) as mszx:
        mszx.decompress(out_dir)

    # mzML named after the archive stem.
    mzml = out_dir / f"{Path(mszx_file_path).stem}.mzML"
    assert mzml.exists()
    assert mzml.stat().st_size > 0

    # Annotations present (suffix .zst stripped).
    files = sorted(p.name for p in out_dir.iterdir())
    # At minimum the mzML and at least one annotation should be present.
    assert mzml.name in files
    annotation_files = [f for f in files if f != mzml.name]
    assert len(annotation_files) > 0
    # No .zst suffix should leak through.
    assert not any(f.endswith(".zst") for f in annotation_files)
