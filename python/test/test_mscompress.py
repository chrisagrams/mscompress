import os
import re
import pytest
import numpy as np
import time
from xml.etree.ElementTree import Element
from mscompress import get_num_threads, get_filesize, MZMLFile, MSZFile, BaseFile, DataFormat, Division, read, Spectra, Spectrum, DataPositions, RuntimeArguments


test_mzml_data = [
    "test/data/test.mzML",
]

test_msz_data = [
    "test/data/test.msz",
]

block_formats = [
    # 1000576, # No comp
    4700001, # ZSTD
    # 4700012, # LZ4
]

# binary_formats = [
#     4700000, # _lossless_
#     4700002, # _cast_64_to_32_
#     # 4700003, # _log2_transform_
#     # 4700004, # _delta16_transform_
#     # 4700005, # _delta24_transform_
#     # 4700006, # _delta32_transform_
#     # 4700007, # _vbr_
#     # 4700008, # _bitpack_
#     # 4700009, # _vdelta16_transform_
#     # 4700010, # _vdelta24_transform_
#     # 4700011, # _cast_64_to_16_
# ]


def test_get_num_threads():
    assert get_num_threads() == os.cpu_count()


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_get_filesize(mzml_file):
    assert get_filesize(mzml_file) == os.path.getsize(mzml_file)


def test_get_filesize_invalid_path():
    with pytest.raises(FileNotFoundError) as e:
        get_filesize("ABC123")


def test_base_file():
    """
    Two functions inside BaseFile will be overridden (compress, decompress, get_mz_binary, get_inten_binary),
    Test if a "base" BaseFile will raise the exception when trying to access it.
    """
    bf = BaseFile(b"ABC", 0, 0)
    with pytest.raises(NotImplementedError) as e:
        bf.compress("out")
    with pytest.raises(NotImplementedError) as e:
        bf.decompress("out")
    with pytest.raises(NotImplementedError) as e:
        bf.get_mz_binary(0)
    with pytest.raises(NotImplementedError) as e:
        bf.get_inten_binary(0)
    with pytest.raises(NotImplementedError) as e:
        bf.get_xml(0)


def test_read_nonexistent_file():
    with pytest.raises(FileNotFoundError) as e:
        f = read("ABC")


def test_read_invalid_file(tmp_path):
    with pytest.raises(OSError) as e:
        f = read(str(tmp_path))


def test_read_invalid_parameter():
    p = {}
    with pytest.raises(ValueError) as e:
        f = read(p)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_read_mzml_file(mzml_file):
    mzml = read(mzml_file)
    assert isinstance(mzml, MZMLFile)
    assert mzml.path == os.path.abspath(mzml_file).encode('utf-8')
    assert mzml.filesize == os.path.getsize(mzml_file)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_read_msz_file(msz_file):
    msz = read(msz_file)
    assert isinstance(msz, MSZFile)
    assert msz.path == os.path.abspath(msz_file).encode('utf-8')
    assert msz.filesize == os.path.getsize(msz_file)


# @pytest.mark.parametrize("mzml_file", test_mzml_data)
# def test_mzml_context_manager(mzml_file):
#     with MZMLFile(mzml_file) as f:
#         assert isinstance(f, MZMLFile)
#         assert f.path == os.path.abspath(mzml_file).encode('utf-8')
#         assert f.filesize == os.path.getsize(mzml_file)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_describe_mzml(mzml_file):
    mzml = read(mzml_file)
    description = mzml.describe()
    assert isinstance(description, dict)
    assert isinstance(description['path'], bytes)
    assert isinstance(description['filesize'], int)
    assert isinstance(description['format'], DataFormat)
    assert isinstance(description['positions'], Division)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_describe_msz(msz_file):
    msz = read(msz_file)
    description = msz.describe()
    assert isinstance(description, dict)
    assert isinstance(description['path'], bytes)
    assert isinstance(description['filesize'], int)
    assert isinstance(description['format'], DataFormat)
    assert isinstance(description['positions'], Division)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_get_mzml_spectra(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    assert isinstance(spectra, Spectra)
    assert isinstance(len(spectra), int) # Test __len__
    for spectrum in spectra: # Test __iter__ + __next__ 
        assert isinstance(spectrum, Spectrum)

    with pytest.raises(IndexError) as e: # Test out of bound IndexError
        spectra[len(spectra) + 1]


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_get_msz_spectra(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    assert isinstance(spectra, Spectra)
    assert isinstance(len(spectra), int) # Test __len__
    for spectrum in spectra: # Test __iter__ + __next__ 
        assert isinstance(spectrum, Spectrum)

    with pytest.raises(IndexError) as e: # Test out of bound IndexError
        spectra[len(spectra) + 1]


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_repr(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    result = repr(spectrum)
    pattern = r"^Spectrum\(index=\d+, scan=\d+, ms_level=\d+, retention_time=(\d+(\.\d+)?|None)\)$"
    assert re.match(pattern, result)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_repr(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    result = repr(spectrum)
    pattern = r"^Spectrum\(index=\d+, scan=\d+, ms_level=\d+, retention_time=(\d+(\.\d+)?|None)\)$"
    assert re.match(pattern, result)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_size(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.size, int)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_size(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.size, int)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_mz(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    mz = spectrum.mz
    assert isinstance(mz, np.ndarray) 


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_mz(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    mz = spectrum.mz
    assert isinstance(mz, np.ndarray)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_inten(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    inten = spectrum.intensity
    assert isinstance(inten, np.ndarray) 


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_inten(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    inten = spectrum.intensity
    assert isinstance(inten, np.ndarray)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_peaks(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    peaks = spectrum.peaks
    assert isinstance(peaks, np.ndarray)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_peaks(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    peaks = spectrum.peaks
    assert isinstance(peaks, np.ndarray)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_xml(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    spec_xml = spectrum.xml
    assert isinstance(spec_xml, Element)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_xml(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    spec_xml = spectrum.xml
    assert isinstance(spec_xml, Element)

@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_ms_level(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.ms_level, int)
    assert spectrum.ms_level > 0


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_spectrum_ms_level(msz_file):
    msz = read(msz_file)
    spectra = msz.spectra
    spectrum = spectra[0]
    assert isinstance(spectrum.ms_level, int)
    assert spectrum.ms_level > 0 


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_spectrum_retention_time(mzml_file):
    mzml = read(mzml_file)
    spectra = mzml.spectra
    spectrum = spectra[0]
    assert spectrum.retention_time is not None
    assert isinstance(spectrum.retention_time, float)

@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_positions(mzml_file):
    mzml = read(mzml_file)
    assert isinstance(mzml.positions, Division)
    assert isinstance(mzml.positions.size, int)
    assert isinstance(mzml.positions.spectra, DataPositions)
    assert isinstance(mzml.positions.xml, DataPositions)
    assert isinstance(mzml.positions.mz, DataPositions)
    assert isinstance(mzml.positions.inten, DataPositions)


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_positions(msz_file):
    msz = read(msz_file)
    assert isinstance(msz.positions, Division)
    assert isinstance(msz.positions.size, int)
    assert isinstance(msz.positions.spectra, DataPositions)
    assert isinstance(msz.positions.xml, DataPositions)
    assert isinstance(msz.positions.mz, DataPositions)
    assert isinstance(msz.positions.inten, DataPositions)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_datapositions(mzml_file):
    mzml = read(mzml_file)
    positions = mzml.positions.spectra
    assert isinstance(positions.start_positions, np.ndarray)
    assert isinstance(positions.end_positions, np.ndarray)
    assert isinstance(positions.total_spec, int)
    assert len(positions.start_positions) == positions.total_spec
    assert len(positions.end_positions) == positions.total_spec


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_mzml_datapositions(msz_file):
    msz = read(msz_file)
    positions = msz.positions.spectra
    assert isinstance(positions.start_positions, np.ndarray)
    assert isinstance(positions.end_positions, np.ndarray)
    assert isinstance(positions.total_spec, int)
    assert len(positions.start_positions) == positions.total_spec
    assert len(positions.end_positions) == positions.total_spec


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_dataformat(mzml_file):
    mzml = read(mzml_file)
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


@pytest.mark.parametrize("msz_file", test_msz_data)
def test_msz_dataformat(msz_file):
    msz = read(msz_file)
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


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_arguments(mzml_file):
    mzml = read(mzml_file)
    assert isinstance(mzml.arguments, RuntimeArguments)


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_arguments_threads(mzml_file):
    mzml = read(mzml_file)
    mzml.arguments.threads = 1
    assert mzml.arguments.threads == 1


@pytest.mark.parametrize("mzml_file", test_mzml_data)
def test_mzml_arguments_zstd_level(mzml_file):
    mzml = read(mzml_file)
    mzml.arguments.zstd_compression_level = 1
    assert mzml.arguments.zstd_compression_level == 1


# @pytest.mark.parametrize(("mzml_file", "format"), zip(test_mzml_data, block_formats))
# def test_target_xml_format(tmp_path, mzml_file, format):
#     mzml = read(mzml_file)
#     mzml.arguments.target_xml_format = format
#     assert mzml.arguments.target_xml_format == format
#     mzml.compress(os.path.join(tmp_path, "test_target_xml_format.msz"))
#     msz = read(os.path.join(tmp_path, "test_target_xml_format.msz"))
#     assert msz.format.target_xml_format == format


# @pytest.mark.parametrize(("mzml_file", "format"), zip(test_mzml_data, block_formats))
# def test_target_mz_format(tmp_path, mzml_file, format):
#     mzml = read(mzml_file)
#     mzml.arguments.target_mz_format = format
#     assert mzml.arguments.target_mz_format == format
#     p = os.path.join(tmp_path, "test_target_mz_format.msz")
#     mzml.compress(p)
#     msz = read(p)
#     assert msz.format.target_mz_format == format


# @pytest.mark.parametrize(("mzml_file", "format"), zip(test_mzml_data, block_formats))
# def test_target_inten_format(tmp_path, mzml_file, format):
#     mzml = read(mzml_file)
#     mzml.arguments.target_inten_format = format
#     assert mzml.arguments.target_inten_format == format
#     p = os.path.join(tmp_path, f"test_target_inten_{format}.msz")
#     mzml.compress(p)
#     msz = read(p)
#     assert msz.format.target_inten_format == format