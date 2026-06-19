"""Tests for filetype sniffing, including plain (non-indexed) mzML.

mscompress reproduces whichever mzML form the source used, so decompressing a
non-indexed source yields plain mzML (a bare ``<mzML>`` root, no ``indexedmzML``
wrapper). ``read()`` must recognize that form too -- not just indexed mzML.
"""

from mscompress.utils import detect_filetype

_PLAIN_MZML = (
    b'<?xml version="1.0" encoding="utf-8"?>'
    b'<mzML xmlns="http://psi.hupo.org/ms/mzml" version="1.1.0" id="parquet">'
    b'<cvList count="1"></cvList></mzML>'
)

_INDEXED_MZML = (
    b'<?xml version="1.0" encoding="utf-8"?>'
    b'<indexedmzML xmlns="http://psi.hupo.org/ms/mzml/1.1/index">'
    b'<mzML xmlns="http://psi.hupo.org/ms/mzml" version="1.1.0"></mzML></indexedmzML>'
)

# msz magic tag 0x035F51B5, little-endian, then arbitrary payload.
_MSZ = (0x035F51B5).to_bytes(4, "little") + b"\x00" * 64


def test_detect_plain_mzml(tmp_path):
    p = tmp_path / "plain.mzML"
    p.write_bytes(_PLAIN_MZML)
    assert detect_filetype(p) == "mzML"


def test_detect_indexed_mzml(tmp_path):
    p = tmp_path / "indexed.mzML"
    p.write_bytes(_INDEXED_MZML)
    assert detect_filetype(p) == "mzML"


def test_detect_msz_magic(tmp_path):
    p = tmp_path / "file.msz"
    p.write_bytes(_MSZ)
    assert detect_filetype(p) == "msz"


def test_detect_unknown(tmp_path):
    p = tmp_path / "random.bin"
    p.write_bytes(b"not a recognized format\x00\x01\x02")
    assert detect_filetype(p) is None


def test_detect_real_fixtures(mzml_file_path, msz_file_path):
    assert detect_filetype(mzml_file_path) == "mzML"
    assert detect_filetype(msz_file_path) == "msz"
