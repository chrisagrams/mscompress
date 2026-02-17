
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
        # Set custom options via arguments properties
        mzml.arguments.threads = 2
        mzml.arguments.blocksize = int(1e7)
        mzml.arguments.mz_scale_factor = 100.0
        mzml.arguments.int_scale_factor = 100.0
        mzml.arguments.zstd_compression_level = 1
        msz = mzml.compress(output_path)
        assert os.path.exists(output_path)
        assert isinstance(msz, MSZFile)

def test_compress_options_invalid_path(mzml_file_path):
    with read(mzml_file_path) as mzml:
        with pytest.raises(TypeError):
             # Just checking if it raises error on invalid types, though cython might handle this differently
             mzml.compress(123)
