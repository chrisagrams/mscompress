"""
Regression tests for corrupt base64 in mzML files (GitHub issue #111/#118).

The test file corrupt_base64.mzML contains 5 spectra where spectrum index 2
has an invalid character injected into its m/z base64 data. mscompress should
handle this gracefully (empty array) instead of crashing.
"""

import numpy as np
import pytest

from mscompress import read, MZMLFile


def test_read_corrupt_base64_mzml(corrupt_base64_mzml_path):
    """Opening a corrupt base64 mzML should not crash."""
    mzml = read(corrupt_base64_mzml_path)
    assert isinstance(mzml, MZMLFile)


def test_corrupt_spectrum_returns_empty_mz(corrupt_base64_mzml_path):
    """Spectrum with corrupt base64 m/z should return an empty array."""
    mzml = read(corrupt_base64_mzml_path)
    mz = mzml.get_mz_binary(2)
    assert isinstance(mz, np.ndarray)
    assert len(mz) == 0


def test_corrupt_spectrum_valid_intensity(corrupt_base64_mzml_path):
    """Spectrum with corrupt m/z should still have valid intensity data."""
    mzml = read(corrupt_base64_mzml_path)
    inten = mzml.get_inten_binary(2)
    assert isinstance(inten, np.ndarray)
    assert len(inten) > 0


def test_valid_spectra_unaffected(corrupt_base64_mzml_path):
    """Non-corrupt spectra in the same file should decode normally."""
    mzml = read(corrupt_base64_mzml_path)
    for i in [0, 1, 3, 4]:
        mz = mzml.get_mz_binary(i)
        inten = mzml.get_inten_binary(i)
        assert len(mz) > 0, f"spectrum {i} mz should not be empty"
        assert len(inten) > 0, f"spectrum {i} inten should not be empty"


def test_dense_sequential_access_no_crash(corrupt_base64_mzml_path):
    """Dense sequential iteration including a corrupt spectrum should not crash."""
    mzml = read(corrupt_base64_mzml_path)
    n = mzml.format.source_total_spec
    assert n == 5
    for i in range(n):
        mz = mzml.get_mz_binary(i)
        inten = mzml.get_inten_binary(i)
        assert isinstance(mz, np.ndarray)
        assert isinstance(inten, np.ndarray)
