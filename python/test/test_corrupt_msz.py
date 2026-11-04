"""
Regression tests for opening a corrupt or truncated .msz file.

read_footer() returns NULL when the footer's magic tag is wrong. MSZFile used
to dereference that NULL and segfault the interpreter; it must raise instead.
"""

import pytest

from mscompress import MSZFile, read


@pytest.fixture
def truncated_msz_path(msz_file_path, tmp_path):
    """test.msz cut to its first 4 KiB: valid header, no footer."""
    path = tmp_path / "truncated.msz"
    with open(msz_file_path, "rb") as src:
        path.write_bytes(src.read(4096))
    return str(path)


@pytest.mark.filterwarnings("ignore::RuntimeWarning")
def test_read_truncated_msz_raises(truncated_msz_path):
    with pytest.raises(ValueError, match="footer is missing or corrupt"):
        read(truncated_msz_path)


@pytest.mark.filterwarnings("ignore::RuntimeWarning")
def test_msz_constructor_truncated_raises(truncated_msz_path):
    with pytest.raises(ValueError, match="footer is missing or corrupt"):
        MSZFile(truncated_msz_path.encode("utf-8"))
