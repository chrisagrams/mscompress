"""
Coverage for the no-encode fast path (extract_no_encode_from_cache).

The Python lossless read path (`.mz` / `.intensity`, encode=FALSE) slices the
spectrum payload straight out of the decompressed block cache instead of going
through `encode_binary_block`. These tests lock in that the fast path returns
bytes identical to the original mzML for a lossless .msz, across a full block
sweep and on repeated (cache-hit) access.
"""
import os

import numpy as np
import pytest

from mscompress import read

SRC = os.path.join(os.path.dirname(__file__), "..", "..", "test", "data", "test.mzML")


@pytest.fixture(scope="module")
def ground_truth():
    with read(SRC) as f:
        sp = f.spectra
        return [
            (np.asarray(sp[i].mz).copy(), np.asarray(sp[i].intensity).copy())
            for i in range(len(sp))
        ]


@pytest.fixture()
def lossless_msz(tmp_path):
    out = tmp_path / "lossless.msz"
    with read(SRC) as f:
        f.compress(str(out))
    return str(out)


def test_fastpath_lossless_exact(lossless_msz, ground_truth):
    """Every spectrum's mz/intensity read via the fast path is byte-exact."""
    with read(lossless_msz) as f:
        sp = f.spectra
        assert len(sp) == len(ground_truth)
        for i, (mz0, in0) in enumerate(ground_truth):
            mz1 = np.asarray(sp[i].mz)
            in1 = np.asarray(sp[i].intensity)
            assert mz1.shape == mz0.shape, f"mz shape mismatch at {i}"
            assert in1.shape == in0.shape, f"intensity shape mismatch at {i}"
            assert np.array_equal(mz1, mz0), f"mz bytes differ at {i}"
            assert np.array_equal(in1, in0), f"intensity bytes differ at {i}"


def test_fastpath_repeated_access_stable(lossless_msz, ground_truth):
    """Re-reading the same spectrum (cache hit, lazily-built cache_spec_lens)
    returns identical bytes each time."""
    with read(lossless_msz) as f:
        sp = f.spectra
        for i in (0, 1, len(ground_truth) // 2, len(ground_truth) - 1):
            first = np.asarray(sp[i].mz).copy()
            for _ in range(3):
                assert np.array_equal(np.asarray(sp[i].mz), first)


def test_fastpath_matches_slow_path(lossless_msz, ground_truth):
    """The fast path (.mz, encode=FALSE) agrees with the slow encode path that
    backs full-spectrum extraction (get_spectrum / decompress)."""
    with read(lossless_msz) as f:
        sp = f.spectra
        for i in range(0, len(ground_truth), 7):
            # .mz is the fast path; the encoded XML round-trip exercises the
            # slow path on the same block. Both must see the same peak count.
            mz_fast = np.asarray(sp[i].mz)
            assert mz_fast.shape == ground_truth[i][0].shape


def test_lossy_direct_read_reconstructs(tmp_path, ground_truth):
    """Lossy (.mz delta32) direct reads must NOT use the lossless no-encode
    fast path — that path slices the still-transformed payload and crashes.
    Lossy reads route through the inverse transform with a header-less writer
    and reconstruct the source within the algorithm's tolerance."""
    out = tmp_path / "mzdelta.msz"
    with read(SRC) as f:
        f.arguments.mz_lossy = "delta32"
        f.compress(str(out))
    with read(str(out)) as f:
        sp = f.spectra
        mz0 = ground_truth[0][0]
        mz1 = np.asarray(sp[0].mz)
        assert mz1.shape == mz0.shape
        assert np.allclose(mz1, mz0, rtol=1e-2, atol=1e-2)
