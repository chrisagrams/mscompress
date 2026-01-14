import os
import pytest
import re
import tempfile
import numpy as np

from mscompress import read, MZMLFile, MSZFile, DataFormat, Division, Spectra, Spectrum, DataPositions, RuntimeArguments

def test_read_mzml_file(mzml_file_path):
    mzml = read(mzml_file_path)
    assert isinstance(mzml, MZMLFile)
    assert mzml.path == os.path.abspath(mzml_file_path).encode('utf-8')
    assert mzml.filesize == os.path.getsize(mzml_file_path)


def test_mzml_context_manager(mzml_file_path):
    with read(mzml_file_path) as f:
        assert isinstance(f, MZMLFile)
        assert f.path == os.path.abspath(mzml_file_path).encode('utf-8')
        assert f.filesize == os.path.getsize(mzml_file_path)

def test_describe_mzml(mzml_file_path):
    mzml = read(mzml_file_path)
    description = mzml.describe()
    assert isinstance(description, dict)
    assert isinstance(description['path'], bytes)
    assert isinstance(description['filesize'], int)
    assert isinstance(description['format'], DataFormat)
    assert isinstance(description['positions'], Division)

def test_get_mzml_spectra(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    assert isinstance(spectra, Spectra)
    assert isinstance(len(spectra), int) # Test __len__
    for spectrum in spectra: # Test __iter__ + __next__ 
        assert isinstance(spectrum, Spectrum)

    with pytest.raises(IndexError): # Test out of bound IndexError
        spectra[len(spectra) + 1]

def test_mzml_spectrum_repr(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    result = repr(spectrum)
    pattern = r"^Spectrum\(index=\d+, scan=\d+, ms_level=\d+, retention_time=(\d+(\.\d+)?|None)\)$"
    assert re.match(pattern, result)

def test_mzml_spectrum_mz(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    mz = spectrum.mz
    assert isinstance(mz, np.ndarray) 
    assert mz.dtype == np.float64 or mz.dtype == np.float32
    assert mz.size > 0

def test_mzml_spectrum_size(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.size, int)

def test_mzml_spectrum_inten(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    inten = spectrum.intensity
    assert isinstance(inten, np.ndarray)
    assert inten.dtype == np.float64 or inten.dtype == np.float32
    assert inten.size > 0

def test_mzml_spectrum_peaks(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    peaks = spectrum.peaks
    assert isinstance(peaks, np.ndarray)
    assert peaks.size > 0
    assert peaks.dtype == np.float64 or peaks.dtype == np.float32

def test_mzml_spectrum_ms_level(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.ms_level, int)
    assert spectrum.ms_level > 0

def test_mzml_spectrum_retention_time(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert spectrum.retention_time is not None
    assert isinstance(spectrum.retention_time, float)

def test_mzml_positions(mzml_file_path):
    mzml = read(mzml_file_path)
    assert isinstance(mzml.positions, Division)
    assert isinstance(mzml.positions.size, int)
    assert isinstance(mzml.positions.spectra, DataPositions)
    assert isinstance(mzml.positions.xml, DataPositions)
    assert isinstance(mzml.positions.mz, DataPositions)
    assert isinstance(mzml.positions.inten, DataPositions)

def test_mzml_datapositions(mzml_file_path):
    mzml = read(mzml_file_path)
    positions = mzml.positions.spectra
    assert isinstance(positions.start_positions, np.ndarray)
    assert isinstance(positions.end_positions, np.ndarray)
    assert isinstance(positions.total_spec, int)
    assert len(positions.start_positions) == positions.total_spec
    assert len(positions.end_positions) == positions.total_spec

def test_mzml_dataformat(mzml_file_path):
    mzml = read(mzml_file_path)
    format = mzml.format
    assert isinstance(format, DataFormat)
    assert isinstance(format.source_mz_fmt, int)
    assert isinstance(format.source_inten_fmt, int)
    assert isinstance(format.source_compression, int)
    pattern = re.compile(
        r"DataFormat\(source_mz_fmt=\d+, source_inten_fmt=\d+, source_compression=\d+, source_total_spec=\d+\)"
    )
    assert pattern.match(str(format))
    pattern = {
        'source_mz_fmt': re.compile(r'MS:\d+'),
        'source_inten_fmt': re.compile(r'MS:\d+'),
        'source_compression': re.compile(r'MS:\d+'),
        'source_total_spec': re.compile(r'\d+')
    }
    
    result = format.to_dict()
    
    for key, regex in pattern.items():
        assert regex.match(str(result[key]))

def test_mzml_arguments(mzml_file_path):
    mzml = read(mzml_file_path)
    assert isinstance(mzml.arguments, RuntimeArguments)

def test_mzml_arguments_threads(mzml_file_path):
    mzml = read(mzml_file_path)
    mzml.arguments.threads = 1
    assert mzml.arguments.threads == 1

def test_mzml_arguments_zstd_level(mzml_file_path):
    mzml = read(mzml_file_path)
    mzml.arguments.zstd_compression_level = 1
    assert mzml.arguments.zstd_compression_level == 1

def test_compress_mzml_file(mzml_file_path, tmp_path):
    output_path = tmp_path / "test_output.msz"
    with read(mzml_file_path) as mzml:    
        # Compress the mzML file to MSZ
        msz = mzml.compress(output_path)
        assert output_path.exists()
        assert output_path.stat().st_size > 0
        assert isinstance(msz, MSZFile)

def test_decompress_mzml_file(mzml_file_path):
    with pytest.raises(NotImplementedError):
        mzml = read(mzml_file_path)
        mzml.decompress("test.out")

def test_retention_time(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    for spectrum in spectra:
        rt = spectrum.retention_time
        assert isinstance(rt, float)
        assert rt >= 0
    
    ## Test specific known retention time
    spectrum = spectra[0]
    assert abs(spectrum.retention_time - 0.21442476) < 1e-6

    spectrum = spectra[10]
    assert abs(spectrum.retention_time - 1.15352136) < 1e-6