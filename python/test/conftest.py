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
def legacy_mszx_file_path():
    """Checked-in v1 archive that stores raw pepXML (pre-mzTab layout)."""
    return str(_TEST_DATA_DIR / "mszx" / "test.mszx")


@pytest.fixture(scope="session")
def _mszx_with_psms(tmp_path_factory):
    # The legacy archive's spectra and pepXML, rebuilt with the PSMs as mzTab.
    import tarfile

    from mscompress import read
    from mscompress.annotations import PepXML
    from mscompress.mszx import MSZXBuilder

    out_dir = tmp_path_factory.mktemp("mszx")
    with tarfile.open(_TEST_DATA_DIR / "mszx" / "test.mszx") as tar:
        tar.extract("temp.msz", out_dir, filter="data")
        pepxml = tar.extractfile("HSA.pepXML.zst").read()
    out = out_dir / "test.mszx"
    with read(str(out_dir / "temp.msz")) as msz:
        MSZXBuilder(msz).add_psms(PepXML().read(pepxml), name="HSA.pepXML").save(out)
    return str(out)


@pytest.fixture
def mszx_file_path(_mszx_with_psms):
    """v1 archive with mzTab search results (HSA.pepXML.mztab.zst)."""
    return _mszx_with_psms

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
