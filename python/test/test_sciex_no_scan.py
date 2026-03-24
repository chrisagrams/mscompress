"""Regression tests for GitHub issue #103.

Sciex TTOF6600 mzML files use spectrum IDs like
  sample=1 period=1 cycle=1 experiment=1
instead of containing a scan= attribute. This caused a segfault in get_scan()
due to NULL pointer arithmetic, and O(n^2) scanning from unbounded strstr.
"""
import tempfile
from pathlib import Path

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
