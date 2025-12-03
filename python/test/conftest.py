import pytest

@pytest.fixture
def mzml_file_path():
    return "test/data/test.mzML"


@pytest.fixture
def msz_file_path():
    return "test/data/test.msz"