
import os
import pytest
from mscompress import MSZFile, MZMLFile, read

def test_compress_options_defaults(mzml_file_path, tmp_path):
    output_path = tmp_path / "default.msz"
    with read(mzml_file_path) as mzml:
        # Test defaults
        msz = mzml.compress(output_path)
        assert os.path.exists(output_path)
        assert isinstance(msz, MSZFile)

def test_compress_options_custom(mzml_file_path, tmp_path):
    output_path = tmp_path / "custom.msz"
    with read(mzml_file_path) as mzml:
        # Test custom options
        msz = mzml.compress(
            output_path,
            threads=2,
            mz_lossy="lossless",
            int_lossy="lossless",
            blocksize=int(1e7),
            mz_scale_factor=100.0,
            int_scale_factor=100.0,
            zstd_compression_level=1
        )
        assert os.path.exists(output_path)
        assert isinstance(msz, MSZFile)
        # We can't easily verify the internal options were set without inspecting logs or side effects,
        # but at least we check it runs without error and produces a file.

def test_compress_options_invalid_path(mzml_file_path):
    with read(mzml_file_path) as mzml:
        with pytest.raises(TypeError):
             # Just checking if it raises error on invalid types, though cython might handle this differently
             mzml.compress(123)
