"""
Regression tests for mzML files without a compression CV term.

When a binaryDataArray has neither MS:1000574 (zlib compression) nor
MS:1000576 (no compression), source_compression used to be left
uninitialized. The decode function pointers ended up NULL and reading a
spectrum segfaulted. The compression is now inferred from the first
non-empty spectrum, and an error is raised when it cannot be determined.

Fixtures live in test/data/no_compression_term/ (a subdirectory so that
directory-wide dataset tests don't pick up the deliberately unreadable one)
and are derived from nonindexed_test.mzML (1 spectrum, 1407 peaks, 64-bit
m/z and intensity):
  - none.mzML:      uncompressed data, term removed
  - zlib.mzML:      zlib-compressed data, term removed
  - uncertain.mzML: defaultArrayLength does not match the data, so
                    inference must fail
"""

import numpy as np
import pytest

from mscompress import MZMLFile, read

_ZLIB = 1000574
_NO_COMP = 1000576


@pytest.fixture
def fixture_dir(test_data_dir):
    return f"{test_data_dir}/no_compression_term"


@pytest.fixture
def reference_mzml(test_data_dir):
    return read(f"{test_data_dir}/nonindexed_test.mzML")


@pytest.mark.parametrize(
    "name, expected",
    [("none.mzML", _NO_COMP), ("zlib.mzML", _ZLIB)],
)
def test_infers_source_compression(fixture_dir, reference_mzml, name, expected):
    mzml = read(f"{fixture_dir}/{name}")
    assert isinstance(mzml, MZMLFile)
    assert mzml.format.source_compression == expected

    spectrum = mzml.spectra[0]
    ref = reference_mzml.spectra[0]
    assert len(spectrum.mz) == 1407
    np.testing.assert_array_equal(spectrum.mz, ref.mz)
    np.testing.assert_array_equal(spectrum.intensity, ref.intensity)


@pytest.mark.parametrize(
    "name", ["none.mzML", "zlib.mzML"]
)
def test_compress_inferred(fixture_dir, tmp_path, name):
    mzml = read(f"{fixture_dir}/{name}")
    out = tmp_path / "out.msz"
    mzml.compress(str(out))
    assert out.stat().st_size > 0


def test_uncertain_compression_raises(fixture_dir):
    with pytest.raises(RuntimeError, match="pattern_detect"):
        read(f"{fixture_dir}/uncertain.mzML")
