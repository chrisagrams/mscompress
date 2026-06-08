"""Tests for the bounded block-cache LRU on MSZFile / MSZXFile."""
import gc
import os

import numpy as np
import pytest

from mscompress import MSZFile, read
from mscompress._core import DEFAULT_BLOCK_CACHE
from mscompress.mszx import MSZXFile


def test_default_cache_blocks_is_positive():
    assert DEFAULT_BLOCK_CACHE > 0


def test_clear_cache_keeps_file_readable(msz_file_path):
    with read(msz_file_path) as msz:
        for i in range(min(5, len(msz.spectra))):
            _ = msz.spectra[i].mz
            _ = msz.spectra[i].intensity

        msz.clear_cache()  # should not raise

        expected = np.asarray(msz.spectra[0].mz)
        msz.clear_cache()
        actual = np.asarray(msz.spectra[0].mz)
        assert np.array_equal(expected, actual)


def test_clear_cache_is_noop_when_disabled(msz_file_path):
    with MSZFile(os.fsencode(msz_file_path), cache_blocks=0) as msz:
        msz.clear_cache()
        _ = msz.spectra[0].mz
        msz.clear_cache()
        _ = msz.spectra[0].intensity


def test_random_access_under_lru_matches_unbounded(msz_file_path):
    """LRU eviction must never change the data returned — only the working-set size."""
    with read(msz_file_path) as bounded, \
            MSZFile(os.fsencode(msz_file_path), cache_blocks=0) as unbounded:
        n = min(len(bounded.spectra), 20)
        order = [7 % n, 0, n - 1, n // 2, 1, n - 2, 3, n - 1, 0, 2]
        order = [i for i in order if 0 <= i < n]
        for i in order:
            assert np.array_equal(
                np.asarray(bounded.spectra[i].mz),
                np.asarray(unbounded.spectra[i].mz),
            )
            assert np.array_equal(
                np.asarray(bounded.spectra[i].intensity),
                np.asarray(unbounded.spectra[i].intensity),
            )


def test_lru_does_not_leak_after_close(msz_file_path):
    """Close+reopen cycles should keep RSS flat — no LRU-related retention."""
    psutil = pytest.importorskip("psutil")
    proc = psutil.Process(os.getpid())

    def rss():
        return proc.memory_info().rss

    # Warm-up so first-touch allocations don't bias the comparison.
    with read(msz_file_path) as f:
        for s in f.spectra:
            _ = s.mz
            _ = s.intensity
    gc.collect()

    baseline = rss()
    for _ in range(3):
        with read(msz_file_path) as f:
            for s in f.spectra:
                _ = s.mz
                _ = s.intensity
        gc.collect()
    delta_mb = (rss() - baseline) / 1024 / 1024
    # 32 MB slack covers glibc arena fragmentation; real retention would be much larger.
    assert delta_mb < 32, (
        f"RSS grew {delta_mb:.1f} MB across 3 open/close cycles — possible leak."
    )


def test_mszx_open_accepts_cache_blocks(mszx_file_path):
    with MSZXFile.open(mszx_file_path, cache_blocks=2) as f:
        _ = f.spectra[0].mz
        _ = f.spectra[0].intensity
        f.clear_cache()
        _ = f.spectra[0].mz


def test_from_mszx_accepts_cache_blocks(mszx_file_path):
    """MSZFile.from_mszx should also honor the cache_blocks override."""
    import tarfile

    with tarfile.open(mszx_file_path, "r") as tar:
        msz_name = next(
            (m.name for m in tar.getmembers() if m.name.endswith(".msz")),
            None,
        )
    assert msz_name, f"No .msz entry in {mszx_file_path}"

    with MSZFile.from_mszx(
        os.fsencode(mszx_file_path),
        msz_name.encode(),
        cache_blocks=1,
    ) as f:
        _ = f.spectra[0].mz
        _ = f.spectra[0].intensity
