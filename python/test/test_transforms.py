import numpy as np
import pytest

from mscompress import read, SpectrumDict, SpectrumTransform


def _intensity_threshold(data, threshold=1e3):
    """Keep only peaks with intensity >= threshold."""
    mask = data["intensity"] >= threshold
    return {"mz": data["mz"][mask], "intensity": data["intensity"][mask]}


def _top_n(data, n=5):
    """Keep only the top-n most intense peaks."""
    if len(data["intensity"]) <= n:
        return data
    idx = np.argsort(data["intensity"])[-n:]
    idx = np.sort(idx)
    return {"mz": data["mz"][idx], "intensity": data["intensity"][idx]}


# ---------- set_transform ----------


def test_set_transform_filters_peaks(mzml_file_path):
    with read(mzml_file_path) as f:
        f.spectra.set_transform(_intensity_threshold)
        spec = f.spectra[0]
        # All returned intensities should be >= threshold
        assert (spec.intensity >= 1e3).all()


def test_set_transform_raw_data_unaffected(mzml_file_path):
    with read(mzml_file_path) as f:
        raw_mz = f.spectra[0].raw_mz.copy()
        raw_inten = f.spectra[0].raw_intensity.copy()

        f.spectra.set_transform(_intensity_threshold)
        spec = f.spectra[0]

        # raw accessors still return full arrays
        np.testing.assert_array_equal(spec.raw_mz, raw_mz)
        np.testing.assert_array_equal(spec.raw_intensity, raw_inten)


def test_set_transform_none_clears(mzml_file_path):
    with read(mzml_file_path) as f:
        original_len = len(f.spectra[0].mz)
        f.spectra.set_transform(_intensity_threshold)
        filtered_len = len(f.spectra[0].mz)

        f.spectra.set_transform(None)
        restored_len = len(f.spectra[0].mz)

        assert restored_len == original_len
        # Filtering should have removed some peaks (or at least not added)
        assert filtered_len <= original_len


def test_set_transform_rejects_non_callable(mzml_file_path):
    with read(mzml_file_path) as f:
        with pytest.raises(TypeError, match="callable"):
            f.spectra.set_transform("not a function")


# ---------- with_transform ----------


def test_with_transform_returns_new_spectra(mzml_file_path):
    with read(mzml_file_path) as f:
        original = f.spectra
        transformed = f.spectra.with_transform(_top_n)
        # Different objects
        assert original is not transformed
        # Original unaffected
        assert len(original[0].mz) >= len(transformed[0].mz)


def test_with_transform_rejects_non_callable(mzml_file_path):
    with read(mzml_file_path) as f:
        with pytest.raises(TypeError, match="callable"):
            f.spectra.with_transform(42)


# ---------- paired filtering ----------


def test_paired_mz_intensity_filtering(mzml_file_path):
    """Transform filters both arrays in lockstep — no misalignment."""
    with read(mzml_file_path) as f:
        f.spectra.set_transform(lambda d: _intensity_threshold(d, threshold=0))
        spec = f.spectra[0]
        assert len(spec.mz) == len(spec.intensity)


# ---------- .peaks reflects transforms ----------


def test_peaks_reflects_transform(mzml_file_path):
    with read(mzml_file_path) as f:
        f.spectra.set_transform(_top_n)
        spec = f.spectra[0]
        peaks = spec.peaks
        assert peaks.shape[0] <= 5
        assert peaks.shape[1] == 2
        np.testing.assert_array_equal(peaks[:, 0], spec.mz)
        np.testing.assert_array_equal(peaks[:, 1], spec.intensity)


# ---------- .size uses raw data ----------


def test_size_uses_raw_data(mzml_file_path):
    with read(mzml_file_path) as f:
        raw_size = f.spectra[0].size
        f.spectra.set_transform(_top_n)
        spec = f.spectra[0]
        # size reflects pre-transform length
        assert spec.size == raw_size


# ---------- works with MSZFile ----------


def test_transform_with_msz(msz_file_path):
    with read(msz_file_path) as f:
        f.spectra.set_transform(_intensity_threshold)
        spec = f.spectra[0]
        assert (spec.intensity >= 1e3).all()
        # raw data still intact
        assert len(spec.raw_mz) >= len(spec.mz)


# ---------- works with MZMLFile ----------


def test_transform_with_mzml(mzml_file_path):
    with read(mzml_file_path) as f:
        f.spectra.set_transform(_top_n)
        spec = f.spectra[0]
        assert len(spec.mz) <= 5


# ---------- transform caching ----------


def test_transform_cached_per_spectrum(mzml_file_path):
    """Accessing .mz twice returns the same object (cached)."""
    with read(mzml_file_path) as f:
        f.spectra.set_transform(_top_n)
        spec = f.spectra[0]
        first = spec.mz
        second = spec.mz
        assert first is second


# ---------- type exports ----------


def test_types_importable():
    """SpectrumDict and SpectrumTransform are importable from the package."""
    assert SpectrumDict is not None
    assert SpectrumTransform is not None
