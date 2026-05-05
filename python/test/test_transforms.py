import numpy as np
import pytest

from mscompress import read, MZMLFile, MSZFile, Spectra, Spectrum, SpectrumDict, SpectrumTransform


def _intensity_threshold_transform(data: SpectrumDict) -> SpectrumDict:
    """Filter out peaks below a fixed intensity threshold."""
    mask = data['intensity'] > 100.0
    return {'mz': data['mz'][mask], 'intensity': data['intensity'][mask]}


def _identity_transform(data: SpectrumDict) -> SpectrumDict:
    """Return data unchanged."""
    return {'mz': data['mz'].copy(), 'intensity': data['intensity'].copy()}


# --- set_transform ---

def test_set_transform_mzml(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra
        spectra.set_transform(_intensity_threshold_transform)
        spectrum = spectra[0]
        assert all(spectrum.intensity > 100.0)


def test_set_transform_msz(msz_file_path):
    with read(msz_file_path) as f:
        spectra = f.spectra
        spectra.set_transform(_intensity_threshold_transform)
        spectrum = spectra[0]
        assert all(spectrum.intensity > 100.0)


def test_set_transform_none_clears(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra
        spectra.set_transform(_intensity_threshold_transform)
        spectra.set_transform(None)
        spectrum = spectra[0]
        # With no transform, mz should equal raw_mz
        np.testing.assert_array_equal(spectrum.mz, spectrum.raw_mz)
        np.testing.assert_array_equal(spectrum.intensity, spectrum.raw_intensity)


# --- with_transform ---

def test_with_transform_returns_new_spectra(mzml_file_path):
    with read(mzml_file_path) as f:
        original = f.spectra
        transformed = original.with_transform(_intensity_threshold_transform)
        assert transformed is not original
        assert len(transformed) == len(original)


def test_with_transform_does_not_modify_original(mzml_file_path):
    with read(mzml_file_path) as f:
        original = f.spectra
        original_mz = original[0].mz.copy()
        _ = original.with_transform(_intensity_threshold_transform)
        # Original should be unaffected
        np.testing.assert_array_equal(original[0].mz, original_mz)


# --- raw data unaffected ---

def test_raw_data_unaffected_by_transform(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra
        raw_mz = spectra[0].raw_mz.copy()
        raw_intensity = spectra[0].raw_intensity.copy()

        transformed = f.spectra.with_transform(_intensity_threshold_transform)
        t_spectrum = transformed[0]

        # Raw data on transformed spectrum should match original
        np.testing.assert_array_equal(t_spectrum.raw_mz, raw_mz)
        np.testing.assert_array_equal(t_spectrum.raw_intensity, raw_intensity)


def test_raw_mz_raw_intensity_exist_without_transform(mzml_file_path):
    with read(mzml_file_path) as f:
        spectrum = f.spectra[0]
        assert isinstance(spectrum.raw_mz, np.ndarray)
        assert isinstance(spectrum.raw_intensity, np.ndarray)
        np.testing.assert_array_equal(spectrum.mz, spectrum.raw_mz)
        np.testing.assert_array_equal(spectrum.intensity, spectrum.raw_intensity)


# --- paired filtering ---

def test_paired_filtering(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra.with_transform(_intensity_threshold_transform)
        spectrum = spectra[0]
        # After filtering, mz and intensity must have the same length
        assert len(spectrum.mz) == len(spectrum.intensity)
        # Both should be shorter or equal to raw
        assert len(spectrum.mz) <= len(spectrum.raw_mz)


# --- peaks reflects transforms ---

def test_peaks_reflects_transform(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra.with_transform(_intensity_threshold_transform)
        spectrum = spectra[0]
        peaks = spectrum.peaks
        np.testing.assert_array_equal(peaks[:, 0], spectrum.mz)
        np.testing.assert_array_equal(peaks[:, 1], spectrum.intensity)


# --- size uses raw (pre-transform) ---

def test_size_uses_raw_mz(mzml_file_path):
    with read(mzml_file_path) as f:
        raw_size = f.spectra[0].size

        spectra = f.spectra.with_transform(_intensity_threshold_transform)
        spectrum = spectra[0]
        # size should reflect pre-transform length
        assert spectrum.size == raw_size
        # But transformed mz may be shorter
        assert len(spectrum.mz) <= spectrum.size


# --- caching ---

def test_transform_result_is_cached(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra.with_transform(_identity_transform)
        spectrum = spectra[0]
        mz_first = spectrum.mz
        mz_second = spectrum.mz
        # Should be the exact same object (cached)
        assert mz_first is mz_second


# --- works with MZMLFile and MSZFile ---

def test_transform_with_mzml_file(mzml_file_path):
    with read(mzml_file_path) as f:
        assert isinstance(f, MZMLFile)
        spectra = f.spectra.with_transform(_intensity_threshold_transform)
        for spectrum in spectra:
            assert all(spectrum.intensity > 100.0)
            break  # Just check first spectrum


def test_transform_with_msz_file(msz_file_path):
    with read(msz_file_path) as f:
        assert isinstance(f, MSZFile)
        spectra = f.spectra.with_transform(_intensity_threshold_transform)
        for spectrum in spectra:
            assert all(spectrum.intensity > 100.0)
            break  # Just check first spectrum


# --- type validation ---

def test_set_transform_rejects_non_callable(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra
        with pytest.raises(TypeError):
            spectra.set_transform("not a callable")


def test_set_transform_rejects_int(mzml_file_path):
    with read(mzml_file_path) as f:
        spectra = f.spectra
        with pytest.raises(TypeError):
            spectra.set_transform(42)


def test_spectrum_transform_protocol():
    """Verify that a plain function with the right signature satisfies the protocol."""
    assert isinstance(_intensity_threshold_transform, SpectrumTransform)
    assert not isinstance("not a callable", SpectrumTransform)
