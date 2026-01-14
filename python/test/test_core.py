import os
import re
import pytest
from mscompress import get_num_threads, get_filesize, read

def test_get_num_threads():
    assert get_num_threads() == os.cpu_count()

def test_get_version():
    from mscompress import __version__ # type: ignore
    assert isinstance(__version__, str)
    assert re.match(r'^\d+\.\d+\.\d+$', __version__)

def test_get_filesize(mzml_file_path):
    assert get_filesize(mzml_file_path) == os.path.getsize(mzml_file_path)


def test_get_filesize_invalid_path():
    with pytest.raises(FileNotFoundError):
        get_filesize("ABC123")


def test_read_nonexistent_file():
    with pytest.raises(FileNotFoundError):
        read("ABC")


def test_read_invalid_file(tmp_path):
    with pytest.raises(OSError):
        read(str(tmp_path))


def test_read_invalid_parameter():
    p = {}
    with pytest.raises(TypeError):
        read(p) # type: ignore