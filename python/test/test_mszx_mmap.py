"""Tests for the first-class MSZXFile (Cython) reader.

MSZXFile extends MSZFile and mmaps the embedded MSZ payload directly out
of the .mszx archive at the entry's byte offset. These tests verify:

  - no temp directory is created on open;
  - data read through the mmap'd MSZ is byte-identical to data read from
    the same MSZ extracted to a standalone file;
  - MSZXFile pickles by re-opening the archive (round-trip works);
  - MSZXFile is polymorphic with MSZFile (isinstance check).
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
        after = {e for e in os.listdir(tmpdir) if e.startswith("mszx_")}
        assert after == before, f"new mszx_* entries appeared: {after - before}"
    finally:
        mszx.close()


def test_mszxfile_is_a_mszfile(mszx_file_path):
    """MSZXFile must be polymorphic with MSZFile."""
    with MSZXFile.open(mszx_file_path) as mszx:
        assert isinstance(mszx, MSZFile)


def test_msz_data_integrity_via_mmap(mszx_file_path, standalone_msz_path):
    """Spectra read through the mmap'd MSZ must match the standalone MSZ
    byte-for-byte."""
    direct = MSZFile(str(standalone_msz_path).encode())
    via_archive = MSZXFile.open(mszx_file_path)
    try:
        assert len(via_archive.spectra) == len(direct.spectra)

        # Compare scan numbers — directly off the MSZXFile (no .msz indirection).
        direct_scans = [int(s) for s in direct.positions.scans]
        archive_scans = [int(s) for s in via_archive.positions.scans]
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


def test_pickle_roundtrip_via_archive(mszx_file_path):
    """MSZXFile.__reduce__ re-opens the archive on unpickle, so the
    round-trip yields a fresh, working MSZXFile."""
    mszx = MSZXFile.open(mszx_file_path)
    try:
        data = pickle.dumps(mszx)
    finally:
        mszx.close()

    restored = pickle.loads(data)
    try:
        assert isinstance(restored, MSZXFile)
        assert len(restored.spectra) > 0
        assert restored.annotations is not None
    finally:
        restored.close()


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
