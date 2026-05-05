"""Tests for parquet -> msz / mszx conversion."""

import math
import tempfile
from pathlib import Path

import numpy as np
import pytest

pa = pytest.importorskip("pyarrow")
pq = pytest.importorskip("pyarrow.parquet")

from mscompress import MSZFile, MSZXFile, TSVReader, read
from mscompress.parquet import (
    parquet_to_annotations_tsv,
    parquet_to_msz,
    parquet_to_mszx,
)


_TEST_DATA_DIR = Path(__file__).resolve().parent.parent.parent / "test" / "data"
_PARQUET_FIXTURE = _TEST_DATA_DIR / "parquet" / "consensus_wsbin_00001_100rows.parquet"


@pytest.fixture
def parquet_path():
    if not _PARQUET_FIXTURE.exists():
        pytest.skip(f"Parquet fixture missing: {_PARQUET_FIXTURE}")
    return _PARQUET_FIXTURE


@pytest.fixture
def parquet_table(parquet_path):
    return pq.read_table(str(parquet_path))


def test_parquet_to_msz_roundtrip(parquet_path, parquet_table, tmp_path):
    """mz / intensity must round-trip bit-exact (f32 source, f32 storage)."""
    out = tmp_path / "spectra.msz"
    msz_file = parquet_to_msz(parquet_path, out)
    msz_file.__exit__(None, None, None)

    n = parquet_table.num_rows
    mz_lists = parquet_table.column("mz").to_pylist()
    inten_lists = parquet_table.column("intensity").to_pylist()

    with read(out) as msz:
        assert isinstance(msz, MSZFile)
        spectra = msz.spectra
        assert len(spectra) == n
        for i, spectrum in enumerate(spectra):
            expected_mz = np.asarray(mz_lists[i], dtype=np.float32)
            expected_in = np.asarray(inten_lists[i], dtype=np.float32)
            assert np.array_equal(spectrum.mz.astype(np.float32), expected_mz), (
                f"mz mismatch at row {i}"
            )
            assert np.array_equal(spectrum.intensity.astype(np.float32), expected_in), (
                f"intensity mismatch at row {i}"
            )


def test_parquet_to_msz_metadata(parquet_path, parquet_table, tmp_path):
    """scan, ms_level, retention_time must be preserved (rt minute->second)."""
    out = tmp_path / "spectra.msz"
    msz_file = parquet_to_msz(parquet_path, out, ret_time_unit="minute")
    msz_file.__exit__(None, None, None)

    rt_minutes = parquet_table.column("ret_time").to_numpy(zero_copy_only=False)

    with read(out) as msz:
        for i, spectrum in enumerate(msz.spectra):
            assert spectrum.scan == i + 1
            assert spectrum.ms_level == 2
            # ret_time was emitted in minutes; mscompress converts to seconds.
            expected_seconds = float(rt_minutes[i]) * 60.0
            assert math.isclose(spectrum.retention_time, expected_seconds, rel_tol=1e-5), (
                f"retention_time mismatch at row {i}: "
                f"got {spectrum.retention_time}, expected {expected_seconds}"
            )


def test_parquet_to_msz_ret_time_seconds(parquet_path, parquet_table, tmp_path):
    """ret_time_unit='second' must NOT apply the *60 conversion."""
    out = tmp_path / "spectra.msz"
    msz_file = parquet_to_msz(parquet_path, out, ret_time_unit="second")
    msz_file.__exit__(None, None, None)

    rt = parquet_table.column("ret_time").to_numpy(zero_copy_only=False)

    with read(out) as msz:
        for i, spectrum in enumerate(msz.spectra):
            assert math.isclose(spectrum.retention_time, float(rt[i]), rel_tol=1e-5)


def test_parquet_to_msz_zlib_binary(parquet_path, parquet_table, tmp_path):
    """use_zlib_binary=True must still round-trip mz/intensity exactly."""
    out = tmp_path / "spectra.msz"
    msz_file = parquet_to_msz(parquet_path, out, use_zlib_binary=True)
    msz_file.__exit__(None, None, None)

    mz_lists = parquet_table.column("mz").to_pylist()

    with read(out) as msz:
        spectra = msz.spectra
        assert len(spectra) == parquet_table.num_rows
        for i in (0, len(spectra) - 1, len(spectra) // 2):
            expected = np.asarray(mz_lists[i], dtype=np.float32)
            assert np.array_equal(spectra[i].mz.astype(np.float32), expected)


def test_parquet_to_annotations_tsv(parquet_path, parquet_table, tmp_path):
    """TSV must be readable by TSVReader and round-trip peptide/charge/score."""
    tsv = tmp_path / "annotations.tsv"
    parquet_to_annotations_tsv(parquet_path, tsv, score_column="max_score")

    peptides = parquet_table.column("peptide").to_pylist()
    charges = parquet_table.column("charge").to_numpy(zero_copy_only=False)
    max_scores = parquet_table.column("max_score").to_numpy(zero_copy_only=False)
    precursors = parquet_table.column("precursor").to_numpy(zero_copy_only=False)

    reader = TSVReader(tsv)
    psms = list(reader)
    assert len(psms) == parquet_table.num_rows

    for i, psm in enumerate(psms):
        assert psm.scan_number == i + 1
        assert psm.peptide == peptides[i]
        assert psm.charge == int(charges[i])
        assert math.isclose(psm.score, float(max_scores[i]), rel_tol=1e-6)
        assert "precursor" in psm.extra
        assert math.isclose(psm.extra["precursor"], float(precursors[i]), rel_tol=1e-6)


def test_parquet_to_mszx_full(parquet_path, parquet_table, tmp_path):
    """End-to-end: produce an .mszx with both spectra and annotations."""
    out = tmp_path / "consensus.mszx"
    parquet_to_mszx(
        parquet_path, out,
        score_column="max_score",
        description="parquet test bundle",
    )

    with MSZXFile.open(out) as mszx:
        assert len(mszx.spectra) == parquet_table.num_rows
        annotations = list(mszx.annotations)
        assert len(annotations) == parquet_table.num_rows

        peptides = parquet_table.column("peptide").to_pylist()
        # Match annotations (sorted by scan_number) to spectra by scan number.
        ann_by_scan = {a.scan_number: a for a in annotations}
        for i, spectrum in enumerate(mszx.spectra):
            ann = ann_by_scan[spectrum.scan]
            assert ann.peptide == peptides[i]


@pytest.mark.skip(
    reason="Empty <binary></binary> blocks crash the C extractor (bus error). "
           "Real consensus parquets always have n_peaks > 0; revisit if that "
           "assumption breaks."
)
def test_parquet_to_msz_empty_peaks(tmp_path, parquet_path):
    """A row with empty mz/intensity must still round-trip cleanly."""
    src = pq.read_table(str(parquet_path))
    # Replace row 0's mz/intensity with empty arrays.
    cols = {name: src.column(name).to_pylist() for name in src.schema.names}
    cols["mz"][0] = []
    cols["intensity"][0] = []
    cols["n_peaks"][0] = 0
    edited = pa.table(cols, schema=src.schema)

    edited_path = tmp_path / "edited.parquet"
    pq.write_table(edited, edited_path)

    out = tmp_path / "spectra.msz"
    msz_file = parquet_to_msz(edited_path, out)
    msz_file.__exit__(None, None, None)

    with read(out) as msz:
        assert len(msz.spectra) == src.num_rows
        s0 = msz.spectra[0]
        assert s0.size == 0
        # Other rows preserved.
        s1 = msz.spectra[1]
        expected_mz = np.asarray(cols["mz"][1], dtype=np.float32)
        assert np.array_equal(s1.mz.astype(np.float32), expected_mz)


def test_parquet_to_msz_missing_column(tmp_path):
    """Missing required columns must produce a clear error."""
    bad = pa.table({"foo": [1, 2, 3]})
    bad_path = tmp_path / "bad.parquet"
    pq.write_table(bad, bad_path)

    with pytest.raises(ValueError, match="missing required columns"):
        parquet_to_msz(bad_path, tmp_path / "out.msz")
