"""Tests for rechunk: rewriting MSZ/MSZX files at a different block size."""

import os
import shutil

import numpy as np
import pytest

import mscompress
from mscompress import read, rechunk
from mscompress._core import MSZFile, MSZXFile
from mscompress.rechunk import _parse_blocksize


def _spectra_payload(path):
    """All (mz, intensity) arrays from a file, for round-trip equality checks."""
    with read(path) as f:
        out = []
        for s in f.spectra:
            out.append((np.asarray(s.mz), np.asarray(s.intensity)))
    return out


def _assert_spectra_equal(expected, actual):
    assert len(expected) == len(actual)
    for (e_mz, e_int), (a_mz, a_int) in zip(expected, actual):
        np.testing.assert_array_equal(e_mz, a_mz)
        np.testing.assert_array_equal(e_int, a_int)


# --- blocksize parsing -------------------------------------------------------

@pytest.mark.parametrize("value,expected", [
    (1_000_000, 1_000_000),
    ("1MB", 1_000_000),
    ("8mb", 8_000_000),
    ("500KB", 500_000),
    ("2GB", 2_000_000_000),
    ("1048576", 1_048_576),
    ("4MB", 4_000_000),
])
def test_parse_blocksize_valid(value, expected):
    assert _parse_blocksize(value) == expected


@pytest.mark.parametrize("value", [0, -1, "0", "-5MB", "abc", "MB"])
def test_parse_blocksize_invalid(value):
    with pytest.raises((ValueError, TypeError)):
        _parse_blocksize(value)


def test_parse_blocksize_rejects_bool():
    with pytest.raises(TypeError):
        _parse_blocksize(True)


# --- MSZ: new-file output ----------------------------------------------------

def test_rechunk_new_file_preserves_spectra(msz_file_path, tmp_path):
    original = _spectra_payload(msz_file_path)
    out = tmp_path / "rechunked.msz"

    with read(msz_file_path) as msz:
        result = msz.rechunk(50_000, output=str(out))
        result._cleanup()

    assert out.exists()
    # Original file untouched.
    assert os.path.exists(msz_file_path)
    _assert_spectra_equal(original, _spectra_payload(str(out)))


def test_rechunk_smaller_blocksize_increases_divisions(msz_file_path, tmp_path):
    out = tmp_path / "small.msz"
    with read(msz_file_path) as msz:
        before = msz.n_divisions
        msz.rechunk(50_000, output=str(out))._cleanup()

    with read(str(out)) as after:
        assert after.n_divisions > before


def test_rechunk_standalone_function(msz_file_path, tmp_path):
    original = _spectra_payload(msz_file_path)
    out = tmp_path / "viafunc.msz"

    result = rechunk(msz_file_path, "1MB", output=str(out))
    try:
        assert isinstance(result, MSZFile)
    finally:
        result._cleanup()

    _assert_spectra_equal(original, _spectra_payload(str(out)))


# --- MSZ: in-place output ----------------------------------------------------

def test_rechunk_in_place(msz_file_path, tmp_path):
    original = _spectra_payload(msz_file_path)
    target = tmp_path / "inplace.msz"
    shutil.copy(msz_file_path, target)

    result = rechunk(str(target), 50_000)  # output=None -> in place
    try:
        assert os.path.samefile(result.path.decode(), str(target))
    finally:
        result._cleanup()

    assert target.exists()
    _assert_spectra_equal(original, _spectra_payload(str(target)))


def test_rechunk_in_place_failure_leaves_original_intact(msz_file_path, tmp_path):
    target = tmp_path / "guard.msz"
    shutil.copy(msz_file_path, target)
    original_bytes = target.read_bytes()

    with read(str(target)) as msz:
        with pytest.raises((ValueError, TypeError)):
            msz.rechunk(0)  # invalid blocksize aborts before any replace

    assert target.read_bytes() == original_bytes
    # No stray temp file left behind.
    assert not (tmp_path / "guard.msz.rechunk-tmp").exists()


# --- lossy preservation ------------------------------------------------------

def test_rechunk_preserves_lossy_config(mzml_file_path, tmp_path):
    lossy_msz = tmp_path / "lossy.msz"
    with read(mzml_file_path) as mzml:
        mzml.arguments.mz_lossy = "cast"  # 64-bit -> 32-bit float on m/z
        mzml.compress(str(lossy_msz))._cleanup()

    lossy_payload = _spectra_payload(str(lossy_msz))
    with read(str(lossy_msz)) as src:
        src_config = src._compression_config()
    assert src_config["mz_lossy"] == "cast"

    out = tmp_path / "lossy_rechunked.msz"
    rechunk(str(lossy_msz), 50_000, output=str(out))._cleanup()

    # The re-chunked file must carry the same compression config (lossy algos,
    # scale factors, stream formats) -- only the block size differs.
    with read(str(out)) as dst:
        assert dst._compression_config() == src_config

    # And re-applying the same precision-reducing transform is idempotent: no
    # additional precision is lost relative to the already-lossy source.
    _assert_spectra_equal(lossy_payload, _spectra_payload(str(out)))


# --- MSZX --------------------------------------------------------------------

def test_rechunk_mszx_preserves_spectra_and_annotations(mszx_file_path, tmp_path):
    original = _spectra_payload(mszx_file_path)
    with MSZXFile.open(mszx_file_path) as src:
        original_annotations = sorted(src.annotation_readers.keys())

    out = tmp_path / "rechunked.mszx"
    result = rechunk(mszx_file_path, 50_000, output=str(out))
    try:
        assert isinstance(result, MSZXFile)
        assert sorted(result.annotation_readers.keys()) == original_annotations
    finally:
        result._cleanup()

    assert out.exists()
    _assert_spectra_equal(original, _spectra_payload(str(out)))
