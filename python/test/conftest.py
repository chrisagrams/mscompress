import pytest


@pytest.fixture
def mzml_file_path():
    return "../test/data/test.mzML"


@pytest.fixture
def msz_file_path():
    return "../test/data/test.msz"

@pytest.fixture
def test_data_dir():
    return "../test/data/"

@pytest.fixture
def mszx_file_path():
    return "../test/data/mszx/test.mszx"

@pytest.fixture
def pepxml_file_path():
    return "../test/data/pepXML/test.pepXML"

@pytest.fixture
def percolator_tsv_file_path():
    return "../test/data/percolator/test.tsv"
