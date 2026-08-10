"""Tests for the byte-shuffle transform exposed via RuntimeArguments.shuffle."""

import filecmp

from mscompress import MSZFile, read


def test_shuffle_defaults_off(mzml_file_path):
    with read(mzml_file_path) as mzml:
        assert mzml.arguments.shuffle is False


def test_shuffle_property_roundtrips(mzml_file_path):
    with read(mzml_file_path) as mzml:
        mzml.arguments.shuffle = True
        assert mzml.arguments.shuffle is True
        mzml.arguments.shuffle = False
        assert mzml.arguments.shuffle is False


def test_shuffle_compresses_smaller(mzml_file_path, tmp_path):
    """The shuffle has to actually change the output and shrink it.

    A round-trip-only check would pass even if the flag silently did nothing,
    so compare against an unshuffled baseline of the same input.
    """
    plain_path = tmp_path / "plain.msz"
    shuffled_path = tmp_path / "shuffled.msz"

    with read(mzml_file_path) as mzml:
        assert isinstance(mzml.compress(plain_path), MSZFile)

    with read(mzml_file_path) as mzml:
        mzml.arguments.shuffle = True
        assert isinstance(mzml.compress(shuffled_path), MSZFile)

    plain_size = plain_path.stat().st_size
    shuffled_size = shuffled_path.stat().st_size

    assert not filecmp.cmp(plain_path, shuffled_path, shallow=False), (
        "shuffle produced an identical file to the baseline; the flag did nothing"
    )
    assert shuffled_size < plain_size, (
        f"shuffle did not reduce size: {shuffled_size} vs baseline {plain_size}"
    )


def test_shuffle_roundtrip_is_byte_identical(mzml_file_path, tmp_path):
    """A shuffled archive must decompress back to the exact source mzML."""
    shuffled_path = tmp_path / "shuffled.msz"
    restored_path = tmp_path / "restored.mzML"

    with read(mzml_file_path) as mzml:
        mzml.arguments.shuffle = True
        mzml.compress(shuffled_path)

    with read(shuffled_path) as msz:
        msz.decompress(restored_path)

    assert filecmp.cmp(mzml_file_path, restored_path, shallow=False), (
        "shuffled round trip is not byte-identical to the source mzML"
    )


def test_shuffle_skipped_for_lossy_stream(mzml_file_path, tmp_path):
    """A lossy stream has no fixed post-transform element width.

    The shuffle must be skipped for it rather than corrupting the stream, so
    compression still succeeds and the file still decompresses.
    """
    output_path = tmp_path / "lossy_shuffle.msz"
    restored_path = tmp_path / "lossy_shuffle.mzML"

    with read(mzml_file_path) as mzml:
        mzml.arguments.shuffle = True
        mzml.arguments.mz_lossy = "delta32"
        mzml.compress(output_path)

    assert output_path.stat().st_size > 0

    with read(output_path) as msz:
        msz.decompress(restored_path)

    assert restored_path.stat().st_size > 0
