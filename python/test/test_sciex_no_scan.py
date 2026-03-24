"""Regression tests for GitHub issues #103 and #105.

Issue #103: Sciex TTOF6600 mzML files use spectrum IDs like
  sample=1 period=1 cycle=1 experiment=1
instead of containing a scan= attribute. This caused a segfault in get_scan()
due to NULL pointer arithmetic, and O(n^2) scanning from unbounded strstr.

Issue #105: MSZ read returns swapped dtypes for mz/intensity arrays when the
source mzML has different precisions (e.g. mz=float64, intensity=float32).
"""
import tempfile
from pathlib import Path

import numpy as np
import pytest
from mscompress import read, MZMLFile, MSZFile


def test_read_sciex_mzml(sciex_mzml_file_path):
    """Opening a Sciex mzML (no scan= attribute) should not segfault."""
    mzml = read(sciex_mzml_file_path)
    assert isinstance(mzml, MZMLFile)
    assert len(mzml.spectra) == 100


def test_sciex_scan_numbers_fallback(sciex_mzml_file_path):
    """When scan= is absent, scan numbers should fall back to index+1."""
    mzml = read(sciex_mzml_file_path)
    for i, spectrum in enumerate(mzml.spectra):
        assert spectrum.scan == i + 1, (
            f"Spectrum at index {i} should have scan={i + 1}, got {spectrum.scan}"
        )


def test_sciex_compress_decompress(sciex_mzml_file_path):
    """Compress and decompress a Sciex mzML file without crashing."""
    msz_path = Path(sciex_mzml_file_path).with_suffix('.msz')

    with read(sciex_mzml_file_path) as mzml:
        mzml.compress(output=msz_path)

    assert msz_path.exists()
    assert msz_path.stat().st_size > 0

    try:
        with read(str(msz_path)) as msz:
            assert isinstance(msz, MSZFile)
            assert len(msz.spectra) == 100

            # Verify scan fallback persists through compress/decompress
            for i, spectrum in enumerate(msz.spectra):
                assert spectrum.scan == i + 1
    finally:
        msz_path.unlink(missing_ok=True)


def test_sciex_spectrum_metadata(sciex_mzml_file_path):
    """MS level and retention time should be parsed correctly even without scan=."""
    mzml = read(sciex_mzml_file_path)
    spectrum = mzml.spectra[0]
    assert spectrum.ms_level in (1, 2)
    assert spectrum.retention_time is not None
    assert spectrum.retention_time >= 0


# --- Issue #105 regression tests: dtype swap ---

def test_sciex_msz_dtype_preserved(sciex_mzml_file_path):
    """After compress/decompress, mz should be float64 and intensity float32.

    Regression test for GitHub issue #105: the dtypes were swapped because the
    C core used source_mz_fmt instead of source_inten_fmt when setting up the
    intensity compression/decompression algorithms.
    """
    msz_path = Path(sciex_mzml_file_path).with_suffix('.msz')

    with read(sciex_mzml_file_path) as mzml:
        mzml.compress(output=msz_path)

    try:
        with read(str(msz_path)) as msz:
            assert isinstance(msz, MSZFile)
            # Check an MS2 spectrum (index 49 per the issue report)
            spectrum = msz.spectra[49]
            assert spectrum.mz.dtype == np.float64, (
                f"Expected mz dtype float64, got {spectrum.mz.dtype}"
            )
            assert spectrum.intensity.dtype == np.float32, (
                f"Expected intensity dtype float32, got {spectrum.intensity.dtype}"
            )
    finally:
        msz_path.unlink(missing_ok=True)


def test_sciex_msz_array_lengths_match(sciex_mzml_file_path):
    """mz and intensity arrays must have the same length after round-trip.

    Before the fix, the dtype swap caused a ~4x length mismatch.
    """
    msz_path = Path(sciex_mzml_file_path).with_suffix('.msz')

    with read(sciex_mzml_file_path) as mzml:
        mzml.compress(output=msz_path)

    try:
        with read(str(msz_path)) as msz:
            for i, spectrum in enumerate(msz.spectra):
                mz = spectrum.mz
                intensity = spectrum.intensity
                if len(mz) == 0 and len(intensity) == 0:
                    continue
                assert len(mz) == len(intensity), (
                    f"Spectrum {i}: mz length {len(mz)} != intensity length {len(intensity)}"
                )
    finally:
        msz_path.unlink(missing_ok=True)


def test_sciex_mzml_vs_msz_data_match(sciex_mzml_file_path):
    """Binary data from MSZ round-trip should match the original mzML values."""
    msz_path = Path(sciex_mzml_file_path).with_suffix('.msz')

    with read(sciex_mzml_file_path) as mzml:
        mzml.compress(output=msz_path)
        # Read a few spectra from the original
        orig_mz_0 = mzml.spectra[0].mz.copy()
        orig_inten_0 = mzml.spectra[0].intensity.copy()

    try:
        with read(str(msz_path)) as msz:
            msz_mz_0 = msz.spectra[0].mz
            msz_inten_0 = msz.spectra[0].intensity
            np.testing.assert_array_equal(orig_mz_0, msz_mz_0)
            np.testing.assert_array_equal(orig_inten_0, msz_inten_0)
    finally:
        msz_path.unlink(missing_ok=True)
