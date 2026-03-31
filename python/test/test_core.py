import os
import re
import pytest
import numpy as np
from mscompress import get_num_threads, get_filesize, list_algorithms, read, MSZFile

def test_get_num_threads():
    assert get_num_threads() == os.cpu_count()

def test_get_version():
    from mscompress import __version__ # type: ignore
    assert isinstance(__version__, str)
    assert re.match(r'^\d+\.\d+\.\d+$', __version__)

def test_get_filesize(mzml_file_path):
    assert get_filesize(mzml_file_path) == os.path.getsize(mzml_file_path)


def test_get_filesize_invalid_path():
    with pytest.raises(FileNotFoundError):
        get_filesize("ABC123")


EXPECTED_KEYS = {"name", "target", "description", "default_mz_scale", "default_int_scale", "experimental"}
VALID_TARGETS = {"mz", "intensity"}

def test_list_algorithms_returns_list():
    algos = list_algorithms()
    assert isinstance(algos, list)
    assert len(algos) > 0

def test_list_algorithms_entry_keys():
    for algo in list_algorithms():
        assert set(algo.keys()) == EXPECTED_KEYS

def test_list_algorithms_entry_types():
    for algo in list_algorithms():
        assert isinstance(algo["name"], str) and algo["name"]
        assert isinstance(algo["target"], list) and len(algo["target"]) > 0
        assert all(t in VALID_TARGETS for t in algo["target"])
        assert isinstance(algo["description"], str) and algo["description"]
        assert isinstance(algo["default_mz_scale"], float)
        assert isinstance(algo["default_int_scale"], float)
        assert isinstance(algo["experimental"], bool)

def test_list_algorithms_known_entries():
    algos = {a["name"]: a for a in list_algorithms()}
    # Verify a few well-known algorithms are present
    assert "cast" in algos
    assert algos["cast"]["target"] == ["mz"]
    assert "delta32" in algos
    assert algos["delta32"]["default_mz_scale"] > 0
    assert "log" in algos
    assert algos["log"]["target"] == ["intensity"]
    assert "vbr" in algos
    assert set(algos["vbr"]["target"]) == {"mz", "intensity"}


def _algo_target_params():
    """Generate pytest params for each (algorithm, target) pair."""
    for algo in list_algorithms():
        for target in algo["target"]:
            test_id = f"{algo['name']}-{target}"
            yield pytest.param(algo, target, id=test_id)

@pytest.mark.parametrize("algo,target", _algo_target_params())
def test_algorithm_compress_decompress(algo, target, mzml_file_path, tmp_path):
    """Compress with each registered lossy algorithm per target, decompress, and verify spectra."""
    name = algo["name"]

    msz_path = tmp_path / f"{name}_{target}.msz"
    out_path = tmp_path / f"{name}_{target}.mzML"

    # Compress with lossy algorithm on one target
    with read(mzml_file_path) as mzml:
        if target == "mz":
            mzml.arguments.mz_lossy = name
        else:
            mzml.arguments.int_lossy = name
        msz = mzml.compress(str(msz_path))
        assert isinstance(msz, MSZFile)
        assert os.path.exists(msz_path)

    # Decompress back to mzML
    with read(str(msz_path)) as msz:
        msz.decompress(str(out_path))
        assert os.path.exists(out_path)

    # Verify spectra are readable and non-empty
    with read(str(out_path)) as mzml_check:
        spectra = mzml_check.spectra
        assert len(spectra) > 0
        s = spectra[0]
        assert len(s.mz) > 0
        assert len(s.intensity) > 0
        assert len(s.mz) == len(s.intensity)


def test_read_nonexistent_file():
    with pytest.raises(FileNotFoundError):
        read("ABC")


def test_read_invalid_file(tmp_path):
    with pytest.raises(OSError):
        read(str(tmp_path))


def test_read_invalid_parameter():
    p = {}
    with pytest.raises(TypeError):
        read(p) # type: ignore