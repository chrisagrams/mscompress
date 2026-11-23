"""
C-core errors surface as exceptions carrying the C message.

When a C call fails, the ERROR records it logged become an MSCompressError
(a RuntimeError subclass), raised directly or chained as the __cause__ of the
binding's own exception, instead of separate RuntimeWarnings.
"""

import warnings

import pytest

from mscompress import MSCompressError, MZMLFile, read


def test_mscompress_error_is_runtime_error():
    assert issubclass(MSCompressError, RuntimeError)


def test_failed_extract_chains_c_error(msz_file_path):
    msz = read(msz_file_path)
    with warnings.catch_warnings():
        warnings.simplefilter("error", RuntimeWarning)
        with pytest.raises(ValueError, match="Failed to extract m/z binary") as exc:
            msz.get_mz_binary(10**9)

    cause = exc.value.__cause__
    assert isinstance(cause, MSCompressError)
    assert "Could not find division for spectrum index" in str(cause)
    assert cause.messages and cause.messages[0] == str(cause)


def test_c_errors_do_not_leak_into_next_call(msz_file_path):
    msz = read(msz_file_path)
    with pytest.raises(ValueError):
        msz.get_mz_binary(10**9)

    with warnings.catch_warnings():
        warnings.simplefilter("error", RuntimeWarning)
        assert len(msz.get_mz_binary(0)) > 0


def test_non_mzml_raises_mscompress_error(tmp_path):
    path = tmp_path / "not_really.mzML"
    path.write_bytes(b"<<< definitely not xml >>>" * 64)
    with pytest.raises(MSCompressError, match="Failed to detect mzML pattern"):
        MZMLFile(str(path).encode("utf-8"))
