import os
import re
import tempfile
import pytest
import numpy as np
from xml.etree.ElementTree import Element
from mscompress import MSZFile, MZMLFile, DataFormat, Division, read, Spectra, Spectrum, DataPositions

def test_read_msz_file(msz_file_path):
    msz = read(msz_file_path)
    assert isinstance(msz, MSZFile)
    assert msz.path == os.path.abspath(msz_file_path).encode('utf-8')
    assert msz.filesize == os.path.getsize(msz_file_path)


def test_msz_context_manager(msz_file_path):
    with read(msz_file_path) as f:
        assert isinstance(f, MSZFile)
        assert f.path == os.path.abspath(msz_file_path).encode('utf-8')
        assert f.filesize == os.path.getsize(msz_file_path)


def test_describe_msz(msz_file_path):
    msz = read(msz_file_path)
    description = msz.describe()
    assert isinstance(description, dict)
    assert isinstance(description['path'], bytes)
    assert isinstance(description['filesize'], int)
    assert isinstance(description['format'], DataFormat)
    assert isinstance(description['positions'], Division)


def test_get_msz_spectra(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    assert isinstance(spectra, Spectra)
    assert isinstance(len(spectra), int) # Test __len__
    for spectrum in spectra: # Test __iter__ + __next__ 
        assert isinstance(spectrum, Spectrum)

    with pytest.raises(IndexError): # Test out of bound IndexError
        spectra[len(spectra) + 1]


def test_msz_spectrum_repr(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    result = repr(spectrum)
    pattern = r"^Spectrum\(index=\d+, scan=\d+, ms_level=\d+, retention_time=(\d+(\.\d+)?|None)\)$"
    assert re.match(pattern, result)



def test_msz_spectrum_size(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.size, int)
    assert spectrum.size > 0

def test_msz_spectrum_mz(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    mz = spectrum.mz
    assert isinstance(mz, np.ndarray)
    assert mz.dtype == np.float64 or mz.dtype == np.float32
    assert mz.size > 0


def test_msz_spectrum_inten(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    inten = spectrum.intensity
    assert isinstance(inten, np.ndarray)
    assert inten.dtype == np.float64 or inten.dtype == np.float32
    assert inten.size > 0



def test_msz_spectrum_peaks(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[1]
    peaks = spectrum.peaks
    assert isinstance(peaks, np.ndarray)
    assert peaks.size > 0
    assert peaks.dtype == np.float64 or peaks.dtype == np.float32


def test_mzml_spectrum_xml(mzml_file_path):
    mzml = read(mzml_file_path)
    spectra = mzml.spectra
    spectrum = spectra[0]
    spec_xml = spectrum.xml
    assert isinstance(spec_xml, Element)


def test_msz_spectrum_xml(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    spec_xml = spectrum.xml
    assert isinstance(spec_xml, Element)


def test_msz_spectrum_ms_level(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.ms_level, int)
    assert spectrum.ms_level > 0 


def test_msz_positions(msz_file_path):
    msz = read(msz_file_path)
    assert isinstance(msz.positions, Division)
    assert isinstance(msz.positions.size, int)
    assert isinstance(msz.positions.spectra, DataPositions)
    assert isinstance(msz.positions.xml, DataPositions)
    assert isinstance(msz.positions.mz, DataPositions)
    assert isinstance(msz.positions.inten, DataPositions)


def test_msz_datapositions(msz_file_path):
    msz = read(msz_file_path)
    positions = msz.positions.spectra
    assert isinstance(positions.start_positions, np.ndarray)
    assert isinstance(positions.end_positions, np.ndarray)
    assert isinstance(positions.total_spec, int)
    assert len(positions.start_positions) == positions.total_spec
    assert len(positions.end_positions) == positions.total_spec


def test_msz_dataformat(msz_file_path):
    msz = read(msz_file_path)
    format = msz.format
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

def test_decompress_msz_file(msz_file_path, tmp_path):
    output_path = tmp_path / "test_output.mzML"
    with read(msz_file_path) as msz:
        # Decompress the MSZ file to mzML
        mzml = msz.decompress(output_path)
        assert os.path.exists(output_path)
        assert os.path.getsize(output_path) > 0
        assert isinstance(mzml, MZMLFile)

def test_compress_msz_file(msz_file_path):
    with pytest.raises(NotImplementedError):
        msz = read(msz_file_path)
        msz.compress("test.out")

def test_retention_time(msz_file_path):
    msz = read(msz_file_path)
    spectra = msz.spectra
    for spectrum in spectra:
        rt = spectrum.retention_time
        assert isinstance(rt, float)
        assert rt >= 0
    
    ## Test specific known retention time
    spectrum = spectra[0]
    assert abs(spectrum.retention_time - 0.21442476) < 1e-6

    spectrum = spectra[10]
    assert abs(spectrum.retention_time - 1.15352136) < 1e-6