"""
Coverage for the bounded Spectra object cache (cache_spectra / Spectra LRU).

Replaces the old `[None] * len` placeholder list with an insertion-ordered
LRU dict so random access can't pin one Spectrum object per ever-touched index.
"""
import os

import numpy as np
import pytest

from mscompress import read
from mscompress._core import CACHE_SPECTRA_AUTO, _DEFAULT_CACHE_SPECTRA, MSZFile

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "test", "data", "test.mzML")


@pytest.fixture()
def msz(tmp_path):
    out = tmp_path / "lossless.msz"
    with read(SRC) as f:
        f.compress(str(out))
    return str(out)


def test_auto_is_sentinel():
    assert CACHE_SPECTRA_AUTO == -1


def test_default_cap_positive(msz):
    with read(msz) as f:
        assert f.spectra.cache_cap == _DEFAULT_CACHE_SPECTRA
        assert f.spectra.cache_cap > 0


def test_explicit_cap_honored(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=4) as f:
        assert f.spectra.cache_cap == 4


def test_zero_disables_eviction(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=0) as f:
        sp = f.spectra
        assert sp.cache_cap == 0
        n = min(20, len(sp))
        for i in range(n):
            _ = sp[i]
        # Unbounded: every distinct index retained.
        assert sp.cache_size == n


def test_bounded_cap_evicts(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=4) as f:
        sp = f.spectra
        for i in range(min(12, len(sp))):
            _ = sp[i]
        assert sp.cache_size <= 4


def test_lru_hit_bumps_to_mru(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=3) as f:
        sp = f.spectra
        a, b, c = sp[0], sp[1], sp[2]      # cache: {0,1,2}
        _ = sp[0]                           # touch 0 -> MRU; order {1,2,0}
        _ = sp[3]                           # insert 3, evict oldest (1)
        assert not sp._has_cached(1)
        assert sp._has_cached(0) and sp._has_cached(2) and sp._has_cached(3)


def test_same_instance_returned_on_hit(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=8) as f:
        sp = f.spectra
        assert sp[0] is sp[0]


def test_values_match_across_eviction(msz):
    """Tight cap (forces eviction) returns identical data to unbounded."""
    with MSZFile(os.fsencode(msz), cache_spectra=2) as bounded, \
         MSZFile(os.fsencode(msz), cache_spectra=0) as unbounded:
        n = min(20, len(bounded.spectra))
        for i in range(n):
            assert np.array_equal(
                np.asarray(bounded.spectra[i].mz),
                np.asarray(unbounded.spectra[i].mz),
            )
            assert np.array_equal(
                np.asarray(bounded.spectra[i].intensity),
                np.asarray(unbounded.spectra[i].intensity),
            )


def test_clear_cache(msz):
    with read(msz) as f:
        sp = f.spectra
        _ = sp[0]
        assert sp.cache_size > 0
        sp.clear_cache()
        assert sp.cache_size == 0
        # Still usable.
        assert np.asarray(sp[0].mz).shape[0] >= 0


def test_iteration_completes_under_tight_cap(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=2) as f:
        count = sum(1 for _ in f.spectra)
        assert count == len(f.spectra)


def test_set_transform_invalidates(msz):
    with read(msz) as f:
        sp = f.spectra
        _ = sp[0]
        assert sp.cache_size > 0
        sp.set_transform(None)
        assert sp.cache_size == 0


def test_with_transform_preserves_cap(msz):
    with MSZFile(os.fsencode(msz), cache_spectra=7) as f:
        sp2 = f.spectra.with_transform(None)
        assert sp2.cache_cap == 7
