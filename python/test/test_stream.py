import os
import tempfile
import pytest
from mscompress import MZMLFile, MSZFile, read


def test_compress_stream(mzml_file_path):
    """Compress via stream, verify output is non-empty bytes."""
    with read(mzml_file_path) as mzml:
        chunks = list(mzml.compress_stream())
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_compress_stream_roundtrip(mzml_file_path, tmp_path):
    """Compress via stream, write to file, verify it can be opened as MSZFile."""
    output_path = tmp_path / "stream_output.msz"
    with read(mzml_file_path) as mzml:
        with open(output_path, "wb") as f:
            for chunk in mzml.compress_stream():
                f.write(chunk)

    assert output_path.exists()
    assert output_path.stat().st_size > 0

    # Verify the streamed output is a valid MSZ file
    with read(str(output_path)) as msz:
        assert isinstance(msz, MSZFile)
        assert msz.format.source_total_spec > 0


def test_compress_stream_matches_file(mzml_file_path, tmp_path):
    """Verify stream output is byte-for-byte identical to file output."""
    file_output = tmp_path / "file_output.msz"
    with read(mzml_file_path) as mzml:
        mzml.compress(file_output)

    with open(file_output, "rb") as f:
        file_bytes = f.read()

    with read(mzml_file_path) as mzml:
        stream_bytes = b"".join(mzml.compress_stream())

    assert stream_bytes == file_bytes


def test_decompress_stream(msz_file_path):
    """Decompress via stream, verify output is non-empty bytes."""
    with read(msz_file_path) as msz:
        chunks = list(msz.decompress_stream())
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_decompress_stream_roundtrip(msz_file_path, tmp_path):
    """Decompress via stream, write to file, verify it can be opened as MZMLFile."""
    output_path = tmp_path / "stream_output.mzML"
    with read(msz_file_path) as msz:
        with open(output_path, "wb") as f:
            for chunk in msz.decompress_stream():
                f.write(chunk)

    assert output_path.exists()
    assert output_path.stat().st_size > 0

    # Verify the streamed output is a valid mzML file
    with read(str(output_path)) as mzml:
        assert isinstance(mzml, MZMLFile)
        assert mzml.format.source_total_spec > 0


def test_decompress_stream_matches_file(msz_file_path, tmp_path):
    """Verify stream output is byte-for-byte identical to file output."""
    file_output = tmp_path / "file_output.mzML"
    with read(msz_file_path) as msz:
        msz.decompress(file_output)

    with open(file_output, "rb") as f:
        file_bytes = f.read()

    with read(msz_file_path) as msz:
        stream_bytes = b"".join(msz.decompress_stream())

    assert stream_bytes == file_bytes


def test_extract_stream_msz(msz_file_path):
    """Extract from MSZ via stream, verify output is non-empty bytes."""
    with read(msz_file_path) as msz:
        chunks = list(msz.extract_stream())
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_msz_with_indicies(msz_file_path):
    """Extract specific indices from MSZ via stream."""
    with read(msz_file_path) as msz:
        chunks = list(msz.extract_stream(indicies=[0, 1, 2]))
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_msz_with_ms_level(msz_file_path):
    """Extract specific MS level from MSZ via stream."""
    with read(msz_file_path) as msz:
        chunks = list(msz.extract_stream(ms_level=1))
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_msz_matches_file(msz_file_path, tmp_path):
    """Verify MSZ extract stream output matches file output."""
    file_output = tmp_path / "file_extract.mzML"
    with read(msz_file_path) as msz:
        msz.extract(file_output, indicies=[0, 1, 2])

    with open(file_output, "rb") as f:
        file_bytes = f.read()

    with read(msz_file_path) as msz:
        stream_bytes = b"".join(msz.extract_stream(indicies=[0, 1, 2]))

    assert stream_bytes == file_bytes


def test_extract_stream_mzml(mzml_file_path):
    """Extract from mzML via stream, verify output is non-empty bytes."""
    with read(mzml_file_path) as mzml:
        chunks = list(mzml.extract_stream())
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_mzml_with_indicies(mzml_file_path):
    """Extract specific indices from mzML via stream."""
    with read(mzml_file_path) as mzml:
        chunks = list(mzml.extract_stream(indicies=[0, 1, 2]))
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_mzml_with_ms_level(mzml_file_path):
    """Extract specific MS level from mzML via stream."""
    with read(mzml_file_path) as mzml:
        chunks = list(mzml.extract_stream(ms_level=1))
        assert len(chunks) > 0
        data = b"".join(chunks)
        assert len(data) > 0


def test_extract_stream_mzml_matches_file(mzml_file_path, tmp_path):
    """Verify mzML extract stream output matches file output."""
    file_output = tmp_path / "file_extract.mzML"
    with read(mzml_file_path) as mzml:
        mzml.extract(file_output, indicies=[0, 1, 2])

    with open(file_output, "rb") as f:
        file_bytes = f.read()

    with read(mzml_file_path) as mzml:
        stream_bytes = b"".join(mzml.extract_stream(indicies=[0, 1, 2]))

    assert stream_bytes == file_bytes


def test_stream_chunk_size(msz_file_path):
    """Verify chunk_size parameter affects chunk sizes."""
    small_chunk = 4096
    with read(msz_file_path) as msz:
        chunks = list(msz.decompress_stream(chunk_size=small_chunk))
        # All chunks except possibly the last should be exactly chunk_size
        for chunk in chunks[:-1]:
            assert len(chunk) == small_chunk


def test_stream_early_close(msz_file_path):
    """Verify no crash/leak when iterator is abandoned early."""
    with read(msz_file_path) as msz:
        gen = msz.decompress_stream()
        first_chunk = next(gen)
        assert len(first_chunk) > 0
        # Abandon the generator without consuming all chunks
        gen.close()
