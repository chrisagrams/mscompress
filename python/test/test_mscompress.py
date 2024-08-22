import os
import pytest
from mscompress import get_num_threads, get_filesize, MZMLFile, MSZFile, BaseFile, DataFormat, Division, read


test_mzml_data = [
    "test/data/test.mzML",
]

test_msz_data = [
    "test/data/test.msz",
]


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

