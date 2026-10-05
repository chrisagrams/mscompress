"""Byte-fidelity of decompression against committed archives.

The existing tests only assert that decompression produces a non-empty file, so
nothing checked that the bytes are actually right. The CLI has always compared
them (cli/test/run_test.cmake); the bindings never did.

On failure these report where and how the output diverges, because the known
failure is Windows-only and reachable only through CI.
"""

import filecmp

import pytest

from mscompress import read


def _describe_difference(expected: bytes, actual: bytes) -> str:
    """First divergence plus surrounding context, as a pytest failure message."""
    if len(expected) != len(actual):
        size_note = f"SIZE DIFFERS: expected {len(expected)}, got {len(actual)}"
    else:
        size_note = f"sizes match ({len(expected)})"

    limit = min(len(expected), len(actual))
    offset = next((i for i in range(limit) if expected[i] != actual[i]), limit)

    if offset == limit:
        return f"{size_note}; common prefix identical, output truncated or extended"

    lo = max(0, offset - 32)
    hi = min(limit, offset + 32)
    return (
        f"{size_note}; first difference at byte {offset} "
        f"(expected {expected[offset]:#04x}, got {actual[offset]:#04x})\n"
        f"  expected[{lo}:{hi}] = {expected[lo:hi]!r}\n"
        f"  actual  [{lo}:{hi}] = {actual[lo:hi]!r}"
    )


def test_committed_msz_decompresses_byte_identically(
    msz_file_path, mzml_file_path, tmp_path
):
    """A committed .msz must decompress to exactly the mzML it was made from.

    Round-trip tests cannot catch a platform that encodes and decodes with a
    matching deviation; only committed bytes can.
    """
    restored_path = tmp_path / "restored.mzML"

    with read(msz_file_path) as msz:
        msz.decompress(restored_path)

    if not filecmp.cmp(mzml_file_path, restored_path, shallow=False):
        with open(mzml_file_path, "rb") as f:
            expected = f.read()
        pytest.fail(_describe_difference(expected, restored_path.read_bytes()))


def test_roundtrip_decompresses_byte_identically(mzml_file_path, tmp_path):
    """The same check against an archive written by this build.

    Paired with the test above: if the committed archive fails while this one
    passes, compression and decompression are consistent with each other but
    disagree with the reference bytes.
    """
    msz_path = tmp_path / "roundtrip.msz"
    restored_path = tmp_path / "roundtrip.mzML"

    with read(mzml_file_path) as mzml:
        mzml.compress(msz_path)

    with read(msz_path) as msz:
        msz.decompress(restored_path)

    if not filecmp.cmp(mzml_file_path, restored_path, shallow=False):
        with open(mzml_file_path, "rb") as f:
            expected = f.read()
        pytest.fail(_describe_difference(expected, restored_path.read_bytes()))
