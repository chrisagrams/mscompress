"""
Coverage for the shared, byte-budgeted BlockCache (PR-C).

The shared cache bounds AGGREGATE decompressed-block memory across all files
that share it — the multi-shard random-access dataloader case where per-file
caps summed to hundreds of GB. Key invariants tested here:

- usage is byte-accurate and bounded by budget (+ at most one in-use block)
- closing a file detaches its blocks (usage drops; no dangling pointers)
- eviction never corrupts returned data, even at a 1-byte budget
- cache=0 opts out; default is a shared process-wide singleton
"""
import gc
import os

import numpy as np
import pytest

from mscompress import (
    read,
    BlockCache,
    DEFAULT_CACHE_BYTES,
    get_default_cache,
    set_cache_budget,
    get_cache_usage,
)
from mscompress._core import MSZFile, MSZXFile


@pytest.fixture()
def msz(tmp_path, mzml_file_path):
    out = tmp_path / "lossless.msz"
    with read(mzml_file_path) as f:
        f.compress(str(out))
    return str(out)


@pytest.fixture()
def ground_truth(mzml_file_path):
    with read(mzml_file_path) as f:
        sp = f.spectra
        n = min(20, len(sp))
        return [
            (np.asarray(sp[i].mz).copy(), np.asarray(sp[i].intensity).copy())
            for i in range(n)
        ]


# --- BlockCache object basics ---------------------------------------------

def test_default_budget_constant():
    assert DEFAULT_CACHE_BYTES == 4 * 1024 * 1024 * 1024


def test_blockcache_reports_budget_and_usage():
    c = BlockCache(1024 * 1024)
    assert c.budget == 1024 * 1024
    assert c.usage == 0


def test_invalid_budget_rejected():
    with pytest.raises(ValueError):
        BlockCache(-1)


def test_default_cache_is_singleton():
    assert get_default_cache() is get_default_cache()


def test_read_uses_default_cache_by_default(msz):
    with read(msz) as f:
        assert f.block_cache is get_default_cache()


# --- usage accounting & sharing -------------------------------------------

def test_usage_grows_on_read_and_is_bounded(msz):
    c = BlockCache(64 * 1024 * 1024)
    with read(msz, cache=c) as f:
        _ = np.asarray(f.spectra[0].mz)
        assert c.usage > 0
        assert c.usage <= c.budget


def test_shared_cache_bounds_two_files(msz):
    """Two files sharing one cache contribute to a single usage total."""
    c = BlockCache(64 * 1024 * 1024)
    with read(msz, cache=c) as a, read(msz, cache=c) as b:
        _ = np.asarray(a.spectra[0].mz)
        ua = c.usage
        _ = np.asarray(b.spectra[1].intensity)
        # b's block added to the SAME cache (aggregate grows under one budget).
        assert c.usage >= ua


def test_close_detaches_blocks(msz):
    c = BlockCache(64 * 1024 * 1024)
    f = read(msz, cache=c)
    _ = np.asarray(f.spectra[0].mz)
    assert c.usage > 0
    f._cleanup()
    assert c.usage == 0


def test_no_leak_across_open_close_cycles(msz):
    c = BlockCache(64 * 1024 * 1024)
    for _ in range(5):
        with read(msz, cache=c) as f:
            for i in range(min(10, len(f.spectra))):
                _ = np.asarray(f.spectra[i].mz)
        assert c.usage == 0  # every cycle fully detaches


# --- eviction correctness --------------------------------------------------

def test_tiny_budget_is_byte_exact(msz, ground_truth):
    """A 1-byte budget forces eviction on every read; data must stay exact and
    must not crash (regression for self-eviction UAF)."""
    with read(msz, cache=BlockCache(1)) as f:
        for i, (mz0, in0) in enumerate(ground_truth):
            assert np.array_equal(np.asarray(f.spectra[i].mz), mz0)
            assert np.array_equal(np.asarray(f.spectra[i].intensity), in0)


def test_soft_cap_retains_in_use_block(msz):
    """Budget is a soft cap: the most-recently-touched block is never evicted,
    so usage can be > 0 even under a 1-byte budget."""
    with read(msz, cache=BlockCache(1)) as f:
        _ = np.asarray(f.spectra[0].mz)
        assert f.block_cache.usage > 0


def test_bounded_matches_disabled(msz, ground_truth):
    with read(msz, cache=BlockCache(4096)) as bounded, \
         read(msz, cache=0) as disabled:
        for i in range(len(ground_truth)):
            assert np.array_equal(
                np.asarray(bounded.spectra[i].mz),
                np.asarray(disabled.spectra[i].mz),
            )


def test_get_spectrum_under_tiny_budget(msz):
    """extract_spectra touches 3 blocks per call; must work under tight cap."""
    with read(msz, cache=BlockCache(1)) as f:
        s = f.spectra[0]
        assert len(s.mz) == len(s.intensity)


# --- opt-out & clear -------------------------------------------------------

def test_cache_zero_disables(msz):
    with read(msz, cache=0) as f:
        assert f.block_cache is None
        # Still fully readable (unbounded legacy caching).
        _ = np.asarray(f.spectra[0].mz)


def test_clear_empties_cache(msz, ground_truth):
    c = BlockCache(64 * 1024 * 1024)
    with read(msz, cache=c) as f:
        _ = np.asarray(f.spectra[0].mz)
        assert c.usage > 0
        c.clear()
        assert c.usage == 0
        # File still usable after a clear (re-decompresses on demand). Use a
        # fresh Spectra view so the Spectrum-object cache doesn't short-circuit.
        f.spectra.clear_cache() if hasattr(f.spectra, "clear_cache") else None
        assert np.array_equal(np.asarray(f.spectra[0].mz), ground_truth[0][0])


def test_file_clear_cache(msz):
    c = BlockCache(64 * 1024 * 1024)
    with read(msz, cache=c) as f:
        _ = np.asarray(f.spectra[0].mz)
        assert c.usage > 0
        f.clear_cache()
        assert c.usage == 0


# --- explicit-int budget & set_cache_budget --------------------------------

def test_int_cache_arg_is_private_budget(msz):
    with read(msz, cache=8192) as f:
        assert isinstance(f.block_cache, BlockCache)
        assert f.block_cache.budget == 8192
        assert f.block_cache is not get_default_cache()


def test_set_cache_budget_installs_default(msz):
    original = get_default_cache()
    try:
        c = set_cache_budget(123456)
        assert get_default_cache() is c
        assert c.budget == 123456
        with read(msz) as f:
            assert f.block_cache is c
    finally:
        # Restore a sane default for subsequent tests in the session.
        set_cache_budget(DEFAULT_CACHE_BYTES)


def test_invalid_cache_type_rejected(msz):
    with pytest.raises(TypeError):
        read(msz, cache="big")


# --- MSZX path -------------------------------------------------------------

def test_mszx_accepts_cache(mszx_file_path):
    c = BlockCache(64 * 1024 * 1024)
    with MSZXFile.open(mszx_file_path, cache=c) as f:
        _ = np.asarray(f.spectra[0].mz)
        assert c.usage > 0
    assert c.usage == 0  # detached on close
