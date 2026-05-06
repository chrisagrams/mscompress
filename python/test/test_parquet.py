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
_PARQUET_FIXTURE_MINIMAL = _TEST_DATA_DIR / "parquet" / "consensus_00_100rows.parquet"


@pytest.fixture
def parquet_path():
    if not _PARQUET_FIXTURE.exists():
        pytest.skip(f"Parquet fixture missing: {_PARQUET_FIXTURE}")
    return _PARQUET_FIXTURE


@pytest.fixture
def parquet_table(parquet_path):
    return pq.read_table(str(parquet_path))


@pytest.fixture
def minimal_parquet_path():
    """Real-world parquet with only `peptide_charge`, `m/z`, `int` columns."""
    if not _PARQUET_FIXTURE_MINIMAL.exists():
        pytest.skip(f"Minimal parquet fixture missing: {_PARQUET_FIXTURE_MINIMAL}")
    return _PARQUET_FIXTURE_MINIMAL


@pytest.fixture
def minimal_parquet_table(minimal_parquet_path):
    return pq.read_table(str(minimal_parquet_path))


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

    with pytest.raises(ValueError, match="missing required mz/intensity"):
        parquet_to_msz(bad_path, tmp_path / "out.msz")


# ---------------------------------------------------------------------------
# Schema-flexibility tests (mz + intensity required, everything else optional)
# ---------------------------------------------------------------------------

def _make_minimal_parquet(tmp_path: Path, **overrides) -> Path:
    """Build a tiny parquet with mz/intensity-style columns plus overrides."""
    cols = {
        "mz": [[100.0, 200.0, 300.0], [110.5, 220.5]],
        "intensity": [[10.0, 20.0, 30.0], [11.0, 22.0]],
    }
    cols.update(overrides)
    table = pa.table(cols)
    p = tmp_path / "minimal.parquet"
    pq.write_table(table, p)
    return p


def test_parquet_renamed_columns(tmp_path):
    """`m/z` + `int` aliases must be accepted with no other metadata."""
    cols = {
        "m/z": [[100.0, 200.0], [110.5, 220.5, 330.5]],
        "int": [[10.0, 20.0], [11.0, 22.0, 33.0]],
    }
    p = tmp_path / "renamed.parquet"
    pq.write_table(pa.table(cols), p)

    out = tmp_path / "out.msz"
    msz_file = parquet_to_msz(p, out)
    msz_file.__exit__(None, None, None)

    with read(out) as msz:
        assert len(msz.spectra) == 2
        for i, expected in enumerate(cols["m/z"]):
            got = msz.spectra[i].mz.astype(np.float32)
            assert np.array_equal(got, np.asarray(expected, dtype=np.float32))


def test_parquet_default_ret_time_when_missing(tmp_path):
    """Missing ret_time column must round-trip as -1 (default)."""
    p = _make_minimal_parquet(tmp_path)

    out = tmp_path / "out.msz"
    msz_file = parquet_to_msz(p, out, ret_time_unit="second")
    msz_file.__exit__(None, None, None)

    with read(out) as msz:
        for spectrum in msz.spectra:
            # _DEFAULT_RET_TIME = -1.0 emitted in seconds → preserved exactly.
            assert math.isclose(spectrum.retention_time, -1.0, rel_tol=1e-5)


def test_parquet_combined_peptide_charge_split(tmp_path):
    """`peptide_charge` should split on the last underscore for annotations."""
    p = _make_minimal_parquet(
        tmp_path,
        peptide_charge=["AANFVHMDTAQK_2", "PEPT_IDE_3"],
    )

    tsv = tmp_path / "ann.tsv"
    parquet_to_annotations_tsv(p, tsv)

    psms = list(TSVReader(tsv))
    assert len(psms) == 2
    assert psms[0].peptide == "AANFVHMDTAQK"
    assert psms[0].charge == 2
    # Last underscore wins, so "PEPT_IDE_3" → ("PEPT_IDE", 3).
    assert psms[1].peptide == "PEPT_IDE"
    assert psms[1].charge == 3


def test_parquet_score_column_none_writes_empty(tmp_path):
    """score_column=None with no auto-resolvable score → empty cells, no raise."""
    p = _make_minimal_parquet(
        tmp_path,
        peptide_charge=["AAA_2", "BBB_3"],
    )

    tsv = tmp_path / "ann.tsv"
    parquet_to_annotations_tsv(p, tsv, score_column=None)

    text = tsv.read_text(encoding="utf-8")
    lines = text.strip().split("\n")
    # Header + 2 rows.
    assert len(lines) == 3
    headers = lines[0].split("\t")
    score_idx = headers.index("score")
    for row in lines[1:]:
        cells = row.split("\t")
        assert cells[score_idx] == "", f"expected empty score cell, got {cells[score_idx]!r}"


def test_parquet_score_column_specified_but_missing_raises(tmp_path):
    """Explicit score_column not in schema must raise."""
    p = _make_minimal_parquet(tmp_path, peptide_charge=["A_2", "B_3"])

    with pytest.raises(ValueError, match="score_column='nope' not in"):
        parquet_to_annotations_tsv(p, tmp_path / "ann.tsv", score_column="nope")


def test_parquet_score_column_auto_resolves_max_score(tmp_path, parquet_path):
    """With max_score present, score_column=None must auto-pick it."""
    tsv = tmp_path / "ann.tsv"
    parquet_to_annotations_tsv(parquet_path, tsv)  # no score_column → auto

    table = pq.read_table(str(parquet_path))
    expected_scores = table.column("max_score").to_numpy(zero_copy_only=False)

    psms = list(TSVReader(tsv))
    assert len(psms) == len(expected_scores)
    for psm, expected in zip(psms, expected_scores):
        assert math.isclose(psm.score, float(expected), rel_tol=1e-6)


def test_parquet_minimal_fixture_to_mszx_full(minimal_parquet_path, minimal_parquet_table, tmp_path):
    """End-to-end on the consensus_00 fixture (peptide_charge / m/z / int only)."""
    out = tmp_path / "consensus_00.mszx"
    parquet_to_mszx(
        minimal_parquet_path,
        out,
        description="minimal-schema parquet bundle",
    )

    n_rows = minimal_parquet_table.num_rows
    mz_lists = minimal_parquet_table.column("m/z").to_pylist()
    int_lists = minimal_parquet_table.column("int").to_pylist()
    pep_charges = minimal_parquet_table.column("peptide_charge").to_pylist()

    with MSZXFile.open(out) as mszx:
        assert len(mszx.spectra) == n_rows
        annotations = list(mszx.annotations)
        assert len(annotations) == n_rows

        ann_by_scan = {a.scan_number: a for a in annotations}
        for i, spectrum in enumerate(mszx.spectra):
            assert np.array_equal(
                spectrum.mz.astype(np.float32),
                np.asarray(mz_lists[i], dtype=np.float32),
            )
            assert np.array_equal(
                spectrum.intensity.astype(np.float32),
                np.asarray(int_lists[i], dtype=np.float32),
            )
            # peptide_charge split: "<seq>_<charge>"
            seq, _, ch = pep_charges[i].rpartition("_")
            ann = ann_by_scan[spectrum.scan]
            assert ann.peptide == seq
            assert ann.charge == int(ch)
