"""
Coverage for lazy division metadata (PR-D).

MSZFile no longer flattens the per-spectrum position arrays into one contiguous
heap copy at open (~22.6 GB across 90 shards). Instead it keeps a per-division
prefix-sum table and resolves a global spectrum index to (division, local) on
demand, reading scan/ms_level straight from the mmap-backed per-division arrays.
The flattened `.positions` Division is built lazily, only when accessed.

These tests assert the resolver returns identical metadata to the flattened
path, including across division boundaries.
"""
import os

import numpy as np
import pytest

from mscompress import read
from mscompress._core import MSZFile


@pytest.fixture()
def msz_multi(tmp_path, mzml_file_path):
    """Compress with many divisions so the prefix-sum resolver crosses
    division boundaries (n_threads high -> n_divisions = n_spectra)."""
    out = tmp_path / "multi.msz"
    with read(mzml_file_path) as f:
        f.arguments.threads = 8  # force several divisions
        f.compress(str(out))
    return str(out)


@pytest.fixture()
def source_meta(mzml_file_path):
    with read(mzml_file_path) as f:
        return [(f.spectra[i].scan, int(f.spectra[i].ms_level))
                for i in range(len(f.spectra))]


def test_scan_ms_level_match_source(msz_multi, source_meta):
    with read(msz_multi) as f:
        assert len(f.spectra) == len(source_meta)
        for i, (scan, ms) in enumerate(source_meta):
            assert f.spectra[i].scan == scan, f"scan mismatch at {i}"
            assert int(f.spectra[i].ms_level) == ms, f"ms_level mismatch at {i}"


def test_resolver_matches_flattened_positions(msz_multi):
    """The lazy resolver and the (lazily) flattened .positions arrays agree."""
    with read(msz_multi) as f:
        n = len(f.spectra)
        resolver_scans = np.array([f.spectra[i].scan for i in range(n)])
    with read(msz_multi) as g:
        flat = np.asarray(g.positions.scans)
        assert flat.shape[0] == n
        assert np.array_equal(resolver_scans, flat)


def test_positions_lazy_then_usable(msz_multi):
    """.positions still returns a working Division (built on demand)."""
    with read(msz_multi) as f:
        pos = f.positions
        assert len(np.asarray(pos.spectra.start_positions)) == len(f.spectra)
        assert len(np.asarray(pos.mz.start_positions)) == len(f.spectra)
        # Idempotent: second access returns consistent data.
        assert np.array_equal(
            np.asarray(pos.scans), np.asarray(f.positions.scans)
        )


def test_iteration_metadata_consistent(msz_multi, source_meta):
    with read(msz_multi) as f:
        got = [(s.scan, int(s.ms_level)) for s in f.spectra]
    assert got == source_meta


def test_mz_intensity_still_exact(msz_multi, mzml_file_path):
    with read(mzml_file_path) as f:
        gt = [(np.asarray(f.spectra[i].mz).copy(),
               np.asarray(f.spectra[i].intensity).copy())
              for i in range(len(f.spectra))]
    with read(msz_multi) as f:
        for i, (mz0, in0) in enumerate(gt):
            assert np.array_equal(np.asarray(f.spectra[i].mz), mz0)
            assert np.array_equal(np.asarray(f.spectra[i].intensity), in0)


def test_describe_materializes_positions(msz_multi):
    with read(msz_multi) as f:
        d = f.describe()
        assert "positions" in d
        assert d["positions"] is not None


def test_no_leak_across_open_close(msz_multi):
    """Open/close cycles must free the prefix-sum table and any lazy flatten."""
    for _ in range(10):
        with read(msz_multi) as f:
            for i in range(len(f.spectra)):
                _ = f.spectra[i].scan
            _ = f.positions  # force lazy flatten too
    # No assertion beyond "did not crash / leak"; valgrind covers the rest.
