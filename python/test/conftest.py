from pathlib import Path

import pytest

_TEST_DATA_DIR = Path(__file__).resolve().parent.parent.parent / "test" / "data"


@pytest.fixture
def mzml_file_path():
    return str(_TEST_DATA_DIR / "test.mzML")


@pytest.fixture
def msz_file_path():
    return str(_TEST_DATA_DIR / "test.msz")

@pytest.fixture
def shuffled_msz_file_path():
    """A checked-in byte-shuffled .msz (format 0.2)."""
    return str(_TEST_DATA_DIR / "test_shuffled.msz")

@pytest.fixture
def test_data_dir():
    return str(_TEST_DATA_DIR)

@pytest.fixture
def mszx_file_path():
    return str(_TEST_DATA_DIR / "mszx" / "test.mszx")

@pytest.fixture
def sciex_mzml_file_path():
    return str(_TEST_DATA_DIR / "sciex_ttof6600_100.mzML")


@pytest.fixture
def pepxml_file_path():
    return str(_TEST_DATA_DIR / "pepXML" / "test.pepXML")

@pytest.fixture
def percolator_tsv_file_path():
    return str(_TEST_DATA_DIR / "percolator" / "test.tsv")

@pytest.fixture
def corrupt_base64_mzml_path():
    return str(_TEST_DATA_DIR / "corrupt_base64.mzML")
