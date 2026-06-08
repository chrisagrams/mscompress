"""Tests for the bounded LRU on Spectra._cache."""
import gc
import os
import sys

import numpy as np
import pytest

from mscompress import MSZFile, read
from mscompress._core import CACHE_SPECTRA_AUTO


def test_cache_spectra_auto_is_sentinel():
    # Same convention as cache_blocks: negative means "use default", 0 means
    # "disable eviction" (legacy unbounded), positive is an explicit cap.
    assert CACHE_SPECTRA_AUTO < 0


def test_default_cap_is_positive(msz_file_path):
    with read(msz_file_path) as msz:
        # AUTO should resolve to a positive default — anything else would
        # silently re-enable the unbounded-leak behavior this PR exists to fix.
        assert msz.spectra.cache_cap > 0


def test_explicit_cap_is_honored(msz_file_path):
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=7) as msz:
        assert msz.spectra.cache_cap == 7


def test_zero_disables_eviction(msz_file_path):
    """cache_spectra=0 mirrors cache_blocks=0: no LRU, dict grows unbounded."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=0) as msz:
        assert msz.spectra.cache_cap == 0
        # Touch every spectrum; with cap=0 the cache holds them all.
        for i in range(len(msz.spectra)):
            _ = msz.spectra[i]
        # If the cap were silently >0 here we'd have evicted some.
        cache = next(
            r for r in gc.get_referents(msz.spectra) if isinstance(r, dict)
        )
        assert len(cache) == len(msz.spectra)


def test_bounded_cap_evicts_oldest(msz_file_path):
    """After touching more than `cap` distinct indices, the dict size stays
    pinned at the cap and the oldest-accessed index is the one evicted."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=3) as msz:
        n = len(msz.spectra)
        if n < 5:
            pytest.skip(f"need at least 5 spectra; got {n}")

        for i in range(5):
            _ = msz.spectra[i]

        cache = next(
            r for r in gc.get_referents(msz.spectra) if isinstance(r, dict)
        )
        # Cap respected.
        assert len(cache) == 3
        # Most recent three indices retained; oldest two evicted.
        assert set(cache.keys()) == {2, 3, 4}


def test_lru_hit_bumps_to_mru(msz_file_path):
    """Re-accessing a cached index should keep it from being evicted next."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=3) as msz:
        n = len(msz.spectra)
        if n < 5:
            pytest.skip(f"need at least 5 spectra; got {n}")

        # Fill the LRU with 0, 1, 2. Order (oldest -> newest): 0, 1, 2.
        for i in range(3):
            _ = msz.spectra[i]
        # Touch 0 again — it should become MRU. Order: 1, 2, 0.
        _ = msz.spectra[0]
        # Now inserting 3 should evict 1 (the new oldest), not 0.
        _ = msz.spectra[3]

        cache = next(
            r for r in gc.get_referents(msz.spectra) if isinstance(r, dict)
        )
        assert set(cache.keys()) == {0, 2, 3}


def test_values_correct_after_eviction(msz_file_path):
    """LRU eviction must not change the data returned — only the working set."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=1) as bounded, \
            MSZFile(os.fsencode(msz_file_path), cache_spectra=0) as unbounded:
        n = min(len(bounded.spectra), 10)
        # Walk forward through every index, then back — bounded re-fetches every
        # spectrum because cap=1 evicts immediately.
        order = list(range(n)) + list(reversed(range(n)))
        for i in order:
            assert np.array_equal(
                np.asarray(bounded.spectra[i].mz),
                np.asarray(unbounded.spectra[i].mz),
            )


def test_clear_cache_empties_dict(msz_file_path):
    with read(msz_file_path) as msz:
        for i in range(min(5, len(msz.spectra))):
            _ = msz.spectra[i]

        cache = next(
            r for r in gc.get_referents(msz.spectra) if isinstance(r, dict)
        )
        assert len(cache) > 0
        msz.spectra.clear_cache()
        assert len(cache) == 0

        # File still works after clearing.
        _ = msz.spectra[0].mz


def test_iteration_under_small_cap(msz_file_path):
    """`for sp in spectra` must complete and yield all spectra even when the
    LRU evicts after every step."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=1) as msz:
        count = 0
        for sp in msz.spectra:
            assert sp is not None
            count += 1
        assert count == len(msz.spectra)


def test_set_transform_invalidates_cache(msz_file_path):
    with read(msz_file_path) as msz:
        _ = msz.spectra[0]
        cache = next(
            r for r in gc.get_referents(msz.spectra) if isinstance(r, dict)
        )
        assert len(cache) == 1
        msz.spectra.set_transform(None)  # clears even when transform is None
        assert len(cache) == 0


def test_with_transform_preserves_cap(msz_file_path):
    """with_transform returns a fresh Spectra; the new one should inherit
    the original's LRU cap rather than reverting to AUTO/default."""
    with MSZFile(os.fsencode(msz_file_path), cache_spectra=5) as msz:
        new = msz.spectra.with_transform(None)
        assert new.cache_cap == 5
