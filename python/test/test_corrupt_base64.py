"""
Regression tests for corrupt base64 in mzML files (GitHub issue #111/#118).

The test file corrupt_base64.mzML contains 5 spectra where spectrum index 2
has an invalid character injected into its m/z base64 data. mscompress should
handle this gracefully (empty array) instead of crashing.
"""

import multiprocessing

import numpy as np
import pytest

from mscompress import MSZFile, MZMLFile, read


def test_read_corrupt_base64_mzml(corrupt_base64_mzml_path):
    """Opening a corrupt base64 mzML should not crash."""
    mzml = read(corrupt_base64_mzml_path)
    assert isinstance(mzml, MZMLFile)


def test_corrupt_spectrum_returns_empty_mz(corrupt_base64_mzml_path):
    """Spectrum with corrupt base64 m/z should return an empty array."""
    mzml = read(corrupt_base64_mzml_path)
    mz = mzml.get_mz_binary(2)
    assert isinstance(mz, np.ndarray)
    assert len(mz) == 0


def test_corrupt_spectrum_valid_intensity(corrupt_base64_mzml_path):
    """Spectrum with corrupt m/z should still have valid intensity data."""
    mzml = read(corrupt_base64_mzml_path)
    inten = mzml.get_inten_binary(2)
    assert isinstance(inten, np.ndarray)
    assert len(inten) > 0


def test_valid_spectra_unaffected(corrupt_base64_mzml_path):
    """Non-corrupt spectra in the same file should decode normally."""
    mzml = read(corrupt_base64_mzml_path)
    for i in [0, 1, 3, 4]:
        mz = mzml.get_mz_binary(i)
        inten = mzml.get_inten_binary(i)
        assert len(mz) > 0, f"spectrum {i} mz should not be empty"
        assert len(inten) > 0, f"spectrum {i} inten should not be empty"


def test_dense_sequential_access_no_crash(corrupt_base64_mzml_path):
    """Dense sequential iteration including a corrupt spectrum should not crash."""
    mzml = read(corrupt_base64_mzml_path)
    n = mzml.format.source_total_spec
    assert n == 5
    for i in range(n):
        mz = mzml.get_mz_binary(i)
        inten = mzml.get_inten_binary(i)
        assert isinstance(mz, np.ndarray)
        assert isinstance(inten, np.ndarray)


def _compress_in_subprocess(input_path: str, output_path: str) -> None:
    """Run compress() in a child process so the parent can enforce a timeout."""
    from mscompress import MZMLFile as _MZMLFile

    with _MZMLFile(input_path.encode("utf-8")) as mzml:
        mzml.compress(output_path)


def test_compress_corrupt_base64_does_not_deadlock(corrupt_base64_mzml_path, tmp_path):
    """compress() must return on a corrupt-base64 input, not deadlock.

    Before the fix in fix/compress-gil-deadlock-corrupt-base64, MZMLFile.compress()
    held the GIL during _compress_mzml(). Worker threads that hit the
    `decode_base64 failed` warning blocked in PyGILState_Ensure while the main
    thread was parked in pthread_join — an unrecoverable deadlock. We run
    compress() in a subprocess with a wall-clock budget so a regression fails
    the test instead of hanging CI.
    """
    output_path = tmp_path / "corrupt_roundtrip.msz"

    ctx = multiprocessing.get_context("spawn")
    proc = ctx.Process(
        target=_compress_in_subprocess,
        args=(corrupt_base64_mzml_path, str(output_path)),
    )
    proc.start()
    proc.join(timeout=60)

    if proc.is_alive():
        proc.terminate()
        proc.join(timeout=5)
        if proc.is_alive():
            proc.kill()
            proc.join()
        pytest.fail(
            "MZMLFile.compress() deadlocked on corrupt-base64 input "
            "(GIL/pthread_join regression — see fix/compress-gil-deadlock-corrupt-base64)."
        )

    assert proc.exitcode == 0, f"compress subprocess exited with {proc.exitcode}"
    assert output_path.exists(), "compress() did not produce an output file"
    assert output_path.stat().st_size > 0, "compress() produced an empty output file"


def test_compress_corrupt_base64_roundtrip(corrupt_base64_mzml_path, tmp_path):
    """After compress(), the MSZ should open and report the expected spectrum count."""
    output_path = tmp_path / "corrupt_roundtrip.msz"

    ctx = multiprocessing.get_context("spawn")
    proc = ctx.Process(
        target=_compress_in_subprocess,
        args=(corrupt_base64_mzml_path, str(output_path)),
    )
    proc.start()
    proc.join(timeout=60)

    if proc.is_alive():
        proc.terminate()
        proc.join(timeout=5)
        if proc.is_alive():
            proc.kill()
            proc.join()
        pytest.fail("compress() deadlocked; cannot verify roundtrip.")

    assert proc.exitcode == 0
    assert output_path.exists()

    with MSZFile(str(output_path).encode("utf-8")) as msz:
        assert msz.format.source_total_spec == 5
