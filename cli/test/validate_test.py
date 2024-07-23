import numpy as np
import base64
import zlib
from validate import compare_mzml, compare_binary_data
import io


def truncate_file_in_half(file_path):
    with open(file_path, 'rb') as file:
        data = file.read()
    half_size = len(data) // 2
    truncated_data = data[:half_size]

    return io.BytesIO(truncated_data)


def test_no_comp_duplicate():
    test_file = "test_files/hek_std2_nocomp_subset.mzML"
    org_file = "test_files/hek_std2_nocomp_subset.mzML"
    assert compare_mzml(test_file, org_file, 0.0, 0.0)


def test_zlib_duplicate():
    test_file = "test_files/hek_std2_zlib_subset.mzML"
    org_file = "test_files/hek_std2_zlib_subset.mzML"
    assert compare_mzml(test_file, org_file, 0.0, 0.0)


def test_nocomp_truncated():
    org_file = "test_files/hek_std2_nocomp_subset.mzML"
    truncated_test_file = truncate_file_in_half(org_file)
    assert compare_mzml(truncated_test_file, org_file, 0.0, 0.0) == False


def test_zlib_truncated():
    org_file = "test_files/hek_std2_zlib_subset.mzML"
    truncated_test_file = truncate_file_in_half(org_file)
    assert compare_mzml(truncated_test_file, org_file, 0.0, 0.0) == False


def test_mismatch_encodings():
    org_file = "test_files/hek_std2_nocomp_subset.mzML"
    test_file = "test_files/hek_std2_zlib_subset.mzML"
    assert compare_mzml(test_file, org_file, 0.0, 0.0) == False


def test_missing_spectra():
    test_file = "test_files/hek_std2_zlib_subset_missing_spectra.mzML"
    org_file = "test_files/hek_std2_zlib_subset.mzML"
    assert compare_mzml(test_file, org_file, 0.0, 0.0) == False


def test_compare_binary_data_no_comp_no_delta():
    array_size = 100
    random_floats = np.random.uniform(0.0, 2000.0, array_size)
    random_floats_bytes = random_floats.tobytes()
    b64_string = base64.b64encode(random_floats_bytes).decode('utf-8')

    try:
        compare_binary_data(b64_string, b64_string, "nocomp", 8, 0.0)
        assert True
    except Exception:
        assert False


def test_compare_binary_data_zlib_no_delta():
    array_size = 100
    random_floats = np.random.uniform(0.0, 2000.0, array_size)
    random_floats_bytes = random_floats.tobytes()
    b64_string = base64.b64encode(zlib.compress(random_floats_bytes)).decode('utf-8')

    try:
        compare_binary_data(b64_string, b64_string, "zlib", 8, 0.0)
        assert True
    except Exception:
        assert False


def test_compare_binary_data_no_comp_no_delta_float():
    array_size = 100
    random_floats = np.random.uniform(0.0, 2000.0, array_size).astype(np.float32)
    random_floats_bytes = random_floats.tobytes()
    b64_string = base64.b64encode(random_floats_bytes).decode('utf-8')

    try:
        compare_binary_data(b64_string, b64_string, "nocomp", 4, 0.0)
        assert True
    except Exception:
        assert False


def test_compare_binary_data_zlib_no_delta_float():
    array_size = 100
    random_floats = np.random.uniform(0.0, 2000.0, array_size).astype(np.float32)
    random_floats_bytes = random_floats.tobytes()
    b64_string = base64.b64encode(zlib.compress(random_floats_bytes)).decode('utf-8')

    try:
        compare_binary_data(b64_string, b64_string, "zlib", 4, 0.0)
        assert True
    except Exception:
        assert False


def test_compare_binary_data_no_comp_exceeds_delta():
    array_size = 100
    delta = 0.5
    random_floats = np.random.uniform(0.0, 2000.0, array_size).astype(np.float32)

    random_floats_modified = random_floats + np.random.uniform(delta + 0.1, delta + 1.0, array_size).astype(np.float32)

    random_floats_bytes = random_floats.tobytes()
    random_floats_modified_bytes = random_floats_modified.tobytes()

    b64_string_original = base64.b64encode(random_floats_bytes).decode('utf-8')
    b64_string_modified = base64.b64encode(random_floats_modified_bytes).decode('utf-8')

    try:
        compare_binary_data(b64_string_original, b64_string_modified, "nocomp", 4, delta)
        assert False
    except Exception:
        assert True


def test_compare_binary_data_zlib_exceeds_delta():
    array_size = 100
    delta = 0.5
    random_floats = np.random.uniform(0.0, 2000.0, array_size).astype(np.float32)

    random_floats_modified = random_floats + np.random.uniform(delta + 0.1, delta + 1.0, array_size).astype(np.float32)

    random_floats_bytes = random_floats.tobytes()
    random_floats_modified_bytes = random_floats_modified.tobytes()

    b64_string_original = base64.b64encode(zlib.compress(random_floats_bytes)).decode('utf-8')
    b64_string_modified = base64.b64encode(zlib.compress(random_floats_modified_bytes)).decode('utf-8')

    try:
        compare_binary_data(b64_string_original, b64_string_modified, "zlib", 4, delta)
        assert False
    except Exception:
        assert True
