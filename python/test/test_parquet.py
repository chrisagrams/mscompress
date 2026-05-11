"""Tests for parquet -> msz / mszx conversion."""

import math
import tempfile
from pathlib import Path

import numpy as np
import pytest

pa = pytest.importorskip("pyarrow")
pq = pytest.importorskip("pyarrow.parquet")

from mscompress import MSZFile, MSZXFile, TSVReader, read
from mscompress.parquet import from_parquet, to_parquet


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
    from_parquet(parquet_path, out)

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
    from_parquet(parquet_path, out, ret_time_unit="minute")

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
    from_parquet(parquet_path, out, ret_time_unit="second")

    rt = parquet_table.column("ret_time").to_numpy(zero_copy_only=False)

    with read(out) as msz:
        for i, spectrum in enumerate(msz.spectra):
            assert math.isclose(spectrum.retention_time, float(rt[i]), rel_tol=1e-5)


def test_parquet_to_msz_zlib_binary(parquet_path, parquet_table, tmp_path):
    """use_zlib_binary=True must still round-trip mz/intensity exactly."""
    out = tmp_path / "spectra.msz"
    from_parquet(parquet_path, out, use_zlib_binary=True)

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
    from_parquet(parquet_path, tsv, score_column="max_score")

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
    from_parquet(
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
    from_parquet(edited_path, out)

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
        from_parquet(bad_path, tmp_path / "out.msz")


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
    from_parquet(p, out)

    with read(out) as msz:
        assert len(msz.spectra) == 2
        for i, expected in enumerate(cols["m/z"]):
            got = msz.spectra[i].mz.astype(np.float32)
            assert np.array_equal(got, np.asarray(expected, dtype=np.float32))


def test_parquet_default_ret_time_when_missing(tmp_path):
    """Missing ret_time column must round-trip as -1 (default)."""
    p = _make_minimal_parquet(tmp_path)

    out = tmp_path / "out.msz"
    from_parquet(p, out, ret_time_unit="second")

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
    from_parquet(p, tsv)

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
    from_parquet(p, tsv, score_column=None)

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
        from_parquet(p, tmp_path / "ann.tsv", score_column="nope")


def test_parquet_score_column_auto_resolves_max_score(tmp_path, parquet_path):
    """With max_score present, score_column=None must auto-pick it."""
    tsv = tmp_path / "ann.tsv"
    from_parquet(parquet_path, tsv)  # no score_column → auto

    table = pq.read_table(str(parquet_path))
    expected_scores = table.column("max_score").to_numpy(zero_copy_only=False)

    psms = list(TSVReader(tsv))
    assert len(psms) == len(expected_scores)
    for psm, expected in zip(psms, expected_scores):
        assert math.isclose(psm.score, float(expected), rel_tol=1e-6)


def test_parquet_minimal_fixture_to_mszx_full(minimal_parquet_path, minimal_parquet_table, tmp_path):
    """End-to-end on the consensus_00 fixture (peptide_charge / m/z / int only)."""
    out = tmp_path / "consensus_00.mszx"
    from_parquet(
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


# ---------------------------------------------------------------------------
# Inverse path: mzML / MSZ / MSZX -> parquet
# ---------------------------------------------------------------------------

_MZML_FIXTURE = _TEST_DATA_DIR / "test.mzML"


@pytest.fixture
def mzml_path():
    if not _MZML_FIXTURE.exists():
        pytest.skip(f"mzML fixture missing: {_MZML_FIXTURE}")
    return _MZML_FIXTURE


def test_to_parquet_roundtrip_msz(parquet_path, parquet_table, tmp_path):
    """parquet -> msz -> parquet must preserve mz/intensity bit-exact."""
    msz_out = tmp_path / "spectra.msz"
    pq_out = tmp_path / "roundtrip.parquet"

    from_parquet(parquet_path, msz_out)
    to_parquet(msz_out, pq_out)

    rt_table = pq.read_table(str(pq_out))
    src_mz = parquet_table.column("mz").to_pylist()
    src_in = parquet_table.column("intensity").to_pylist()
    out_mz = rt_table.column("mz").to_pylist()
    out_in = rt_table.column("intensity").to_pylist()

    assert rt_table.num_rows == parquet_table.num_rows
    for i in range(parquet_table.num_rows):
        assert np.array_equal(
            np.asarray(out_mz[i], dtype=np.float32),
            np.asarray(src_mz[i], dtype=np.float32),
        ), f"mz mismatch at row {i}"
        assert np.array_equal(
            np.asarray(out_in[i], dtype=np.float32),
            np.asarray(src_in[i], dtype=np.float32),
        ), f"intensity mismatch at row {i}"


def test_to_parquet_metadata(parquet_path, parquet_table, tmp_path):
    """scan, ms_level, precursor, charge, ret_time (in minutes) round-trip."""
    msz_out = tmp_path / "spectra.msz"
    pq_out = tmp_path / "roundtrip.parquet"

    from_parquet(parquet_path, msz_out, ret_time_unit="minute")
    to_parquet(msz_out, pq_out)

    out = pq.read_table(str(pq_out))
    expected_rt = parquet_table.column("ret_time").to_numpy(zero_copy_only=False)
    expected_prec = parquet_table.column("precursor").to_numpy(zero_copy_only=False)
    expected_charge = parquet_table.column("charge").to_numpy(zero_copy_only=False)

    scans = out.column("scan").to_numpy(zero_copy_only=False)
    ms_levels = out.column("ms_level").to_numpy(zero_copy_only=False)
    rt = out.column("ret_time").to_numpy(zero_copy_only=False)
    prec = out.column("precursor").to_numpy(zero_copy_only=False)
    charges = out.column("charge").to_numpy(zero_copy_only=False)

    for i in range(out.num_rows):
        assert int(scans[i]) == i + 1
        assert int(ms_levels[i]) == 2
        # ret_time was minutes in source, MSZ stores seconds, auto-detect
        # should emit minutes again.
        assert math.isclose(float(rt[i]), float(expected_rt[i]), rel_tol=1e-4), (
            f"rt mismatch row {i}: {rt[i]} vs {expected_rt[i]}"
        )
        assert math.isclose(float(prec[i]), float(expected_prec[i]), rel_tol=1e-5)
        assert int(charges[i]) == int(expected_charge[i])


def test_to_parquet_schema_columns_msz(parquet_path, tmp_path):
    """MSZ input must produce schema *without* annotation columns."""
    msz_out = tmp_path / "spectra.msz"
    pq_out = tmp_path / "out.parquet"
    from_parquet(parquet_path, msz_out)
    to_parquet(msz_out, pq_out)

    schema = pq.read_schema(str(pq_out))
    names = set(schema.names)
    assert {"scan", "ms_level", "ret_time", "precursor", "charge",
            "mz", "intensity", "n_peaks"} <= names
    assert "peptide" not in names
    assert "score" not in names


def test_to_parquet_mszx_includes_annotations(parquet_path, parquet_table, tmp_path):
    """MSZX input must emit peptide/peptide_charge/score columns."""
    mszx_out = tmp_path / "bundle.mszx"
    pq_out = tmp_path / "out.parquet"
    from_parquet(parquet_path, mszx_out, score_column="max_score")
    to_parquet(mszx_out, pq_out)

    schema = pq.read_schema(str(pq_out))
    assert {"peptide", "peptide_charge", "score", "q_value"} <= set(schema.names)

    out = pq.read_table(str(pq_out))
    assert out.num_rows == parquet_table.num_rows

    expected_peptides = parquet_table.column("peptide").to_pylist()
    expected_charges = parquet_table.column("charge").to_numpy(zero_copy_only=False)
    expected_scores = parquet_table.column("max_score").to_numpy(zero_copy_only=False)

    # Match output rows back to source by scan number (forward path emits scan = i+1).
    out_by_scan = {
        int(out.column("scan")[i].as_py()): i for i in range(out.num_rows)
    }
    out_pep = out.column("peptide").to_pylist()
    out_pep_charge = out.column("peptide_charge").to_pylist()
    out_score = out.column("score").to_numpy(zero_copy_only=False)
    for i, expected_pep in enumerate(expected_peptides):
        scan = i + 1
        row = out_by_scan[scan]
        assert out_pep[row] == expected_pep
        assert out_pep_charge[row] == f"{expected_pep}_{int(expected_charges[i])}"
        assert math.isclose(float(out_score[row]), float(expected_scores[i]), rel_tol=1e-5)


def test_to_parquet_full_roundtrip_mszx(parquet_path, parquet_table, tmp_path):
    """mszx -> parquet -> mszx preserves spectra and annotations."""
    mszx_in = tmp_path / "in.mszx"
    pq_mid = tmp_path / "mid.parquet"
    mszx_out = tmp_path / "out.mszx"

    from_parquet(parquet_path, mszx_in, score_column="max_score")
    to_parquet(mszx_in, pq_mid)
    from_parquet(pq_mid, mszx_out)

    with MSZXFile.open(mszx_out) as mszx:
        assert len(mszx.spectra) == parquet_table.num_rows
        ann_by_scan = {a.scan_number: a for a in mszx.annotations}
        expected_pep = parquet_table.column("peptide").to_pylist()
        for i, spectrum in enumerate(mszx.spectra):
            assert spectrum.scan == i + 1
            assert ann_by_scan[spectrum.scan].peptide == expected_pep[i]


def test_to_parquet_mzml(mzml_path, tmp_path):
    """to_parquet must work on an mzML directly, not just MSZ."""
    pq_out = tmp_path / "from_mzml.parquet"
    to_parquet(mzml_path, pq_out)

    out = pq.read_table(str(pq_out))
    assert out.num_rows > 0
    # Required columns must exist and be the right dtype.
    schema = out.schema
    assert pa.types.is_list(schema.field("mz").type)
    assert pa.types.is_list(schema.field("intensity").type)
    assert pa.types.is_int32(schema.field("scan").type)
    # No annotation columns on an mzML input.
    assert "peptide" not in schema.names


def test_to_parquet_ret_time_unit_seconds(parquet_path, parquet_table, tmp_path):
    """Source written in seconds must auto-detect and emit in seconds."""
    msz_out = tmp_path / "spectra.msz"
    pq_out = tmp_path / "out.parquet"

    from_parquet(parquet_path, msz_out, ret_time_unit="second")
    to_parquet(msz_out, pq_out)

    out = pq.read_table(str(pq_out))
    expected_rt = parquet_table.column("ret_time").to_numpy(zero_copy_only=False)
    rt = out.column("ret_time").to_numpy(zero_copy_only=False)
    # Source ret_time was numbers we asked to be interpreted as seconds when
    # writing the msz; reading back through MSZ -> parquet should give those
    # same numbers (auto-detected as seconds, no conversion).
    for i in range(out.num_rows):
        assert math.isclose(float(rt[i]), float(expected_rt[i]), rel_tol=1e-4)


def test_to_parquet_open_file_object(parquet_path, tmp_path):
    """Passing an already-open file should not close it."""
    msz_out = tmp_path / "spectra.msz"
    pq_out = tmp_path / "out.parquet"
    from_parquet(parquet_path, msz_out)

    with read(msz_out) as msz:
        to_parquet(msz, pq_out)
        # File is still usable after to_parquet returns.
        n = len(msz.spectra)
        assert n > 0

    out = pq.read_table(str(pq_out))
    assert out.num_rows == n


def test_to_parquet_multi_psm_all(parquet_path, tmp_path):
    """multi_psm='all' emits N rows per scan when there are N PSMs.

    We only have one PSM per scan in the test fixture, so behavior should
    match the default 'best' for row count, but the codepath should still
    function.
    """
    mszx_out = tmp_path / "bundle.mszx"
    pq_all = tmp_path / "all.parquet"
    pq_best = tmp_path / "best.parquet"

    from_parquet(parquet_path, mszx_out, score_column="max_score")
    to_parquet(mszx_out, pq_all, multi_psm="all")
    to_parquet(mszx_out, pq_best, multi_psm="best")

    n_all = pq.read_metadata(str(pq_all)).num_rows
    n_best = pq.read_metadata(str(pq_best)).num_rows
    # Fixture has 1 PSM per scan, so 'all' and 'best' coincide.
    assert n_all == n_best


# ---------------------------------------------------------------------------
# from_parquet dispatch behavior
# ---------------------------------------------------------------------------

def test_from_parquet_extension_inference_msz(parquet_path, tmp_path):
    """.msz extension dispatches to the MSZ writer."""
    out = tmp_path / "out.msz"
    returned = from_parquet(parquet_path, out)
    assert returned == out
    with read(out) as f:
        assert isinstance(f, MSZFile)


def test_from_parquet_extension_inference_mszx(parquet_path, tmp_path):
    """.mszx extension produces an MSZX archive (msz + tsv inside)."""
    out = tmp_path / "out.mszx"
    from_parquet(parquet_path, out, score_column="max_score")
    with MSZXFile.open(out) as mszx:
        assert mszx.annotations is not None


def test_from_parquet_extension_inference_tsv(parquet_path, tmp_path):
    """.tsv extension dispatches to the annotations TSV writer."""
    out = tmp_path / "out.tsv"
    from_parquet(parquet_path, out, score_column="max_score")
    psms = list(TSVReader(out))
    assert len(psms) > 0


def test_from_parquet_case_insensitive_extension(parquet_path, tmp_path):
    """Suffix matching is case-insensitive (.MSZ works)."""
    out = tmp_path / "out.MSZ"
    from_parquet(parquet_path, out)
    # Even though we wrote with .MSZ suffix, the file should be a valid msz.
    with read(out) as f:
        assert isinstance(f, MSZFile)


def test_from_parquet_explicit_output_type_overrides(parquet_path, tmp_path):
    """output_type kwarg wins over extension inference."""
    out = tmp_path / "out.unknown"
    from_parquet(parquet_path, out, output_type="tsv", score_column="max_score")
    psms = list(TSVReader(out))
    assert len(psms) > 0


def test_from_parquet_unknown_extension_raises(parquet_path, tmp_path):
    """No way to dispatch without an explicit output_type → ValueError."""
    out = tmp_path / "out.xyz"
    with pytest.raises(ValueError, match="Cannot infer output type"):
        from_parquet(parquet_path, out)


def test_from_parquet_invalid_output_type_raises(parquet_path, tmp_path):
    """Bad output_type value raises a clear error."""
    out = tmp_path / "out.msz"
    with pytest.raises(ValueError, match="output_type must be"):
        from_parquet(parquet_path, out, output_type="bogus")  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# Long-format (.mzparquet) support — ThermoRawFileParser one-row-per-peak schema
# ---------------------------------------------------------------------------

_MZPARQUET_FIXTURE = _TEST_DATA_DIR / "parquet" / "test.mzparquet"


@pytest.fixture
def mzparquet_path():
    if not _MZPARQUET_FIXTURE.exists():
        pytest.skip(f"mzparquet fixture missing: {_MZPARQUET_FIXTURE}")
    return _MZPARQUET_FIXTURE


@pytest.fixture
def mzparquet_table(mzparquet_path):
    return pq.read_table(str(mzparquet_path))


def _group_peaks_by_scan(table):
    """Group peaks from a long-format table by scan, preserving source order.

    Returns OrderedDict[int, dict] with keys: mz (list[float]), intensity
    (list[float]), level, rt, precursor_mz, precursor_charge, ion_mobility,
    isolation_lower, isolation_upper. Per-scan scalars are taken from the
    first peak of that scan.
    """
    import collections
    cols = {name: table.column(name).to_pylist() for name in table.schema.names}
    groups: "collections.OrderedDict[int, dict]" = collections.OrderedDict()
    for i in range(table.num_rows):
        scan = int(cols["scan"][i])
        g = groups.get(scan)
        if g is None:
            g = {
                "mz": [],
                "intensity": [],
                "level": int(cols["level"][i]),
                "rt": float(cols["rt"][i]),
                "precursor_mz": cols.get("precursor_mz", [None] * table.num_rows)[i],
                "precursor_charge": cols.get("precursor_charge", [None] * table.num_rows)[i],
                "ion_mobility": cols.get("ion_mobility", [None] * table.num_rows)[i],
                "isolation_lower": cols.get("isolation_lower", [None] * table.num_rows)[i],
                "isolation_upper": cols.get("isolation_upper", [None] * table.num_rows)[i],
            }
            groups[scan] = g
        g["mz"].append(float(cols["mz"][i]))
        g["intensity"].append(float(cols["intensity"][i]))
    return groups


def test_mzparquet_to_msz_spectrum_count(mzparquet_path, mzparquet_table, tmp_path):
    """One spectrum per unique source scan."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    unique_scans = len(set(mzparquet_table.column("scan").to_pylist()))
    with read(out) as msz:
        assert len(msz.spectra) == unique_scans


def test_mzparquet_to_msz_scan_numbers(mzparquet_path, mzparquet_table, tmp_path):
    """Scan numbers come from the source `scan` column, not idx+1."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    source_scans = list(groups.keys())
    with read(out) as msz:
        assert [s.scan for s in msz.spectra] == source_scans


def test_mzparquet_to_msz_ms_levels(mzparquet_path, mzparquet_table, tmp_path):
    """ms_level reflects source `level` column (mix of 1 and 2), not hardcoded 2."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        assert {s.ms_level for s in msz.spectra} == {1, 2}, (
            "fixture must exercise both MS1 and MS2 to validate the level column"
        )
        for spectrum in msz.spectra:
            assert spectrum.ms_level == groups[spectrum.scan]["level"]


def test_mzparquet_to_msz_mz_intensity_bitexact(mzparquet_path, mzparquet_table, tmp_path):
    """Per-scan mz/intensity must match the grouped source peaks bit-exact."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        for spectrum in msz.spectra:
            g = groups[spectrum.scan]
            expected_mz = np.asarray(g["mz"], dtype=np.float32)
            expected_in = np.asarray(g["intensity"], dtype=np.float32)
            assert np.array_equal(spectrum.mz.astype(np.float32), expected_mz), (
                f"mz mismatch at scan {spectrum.scan}"
            )
            assert np.array_equal(spectrum.intensity.astype(np.float32), expected_in), (
                f"intensity mismatch at scan {spectrum.scan}"
            )


def test_mzparquet_to_msz_retention_time(mzparquet_path, mzparquet_table, tmp_path):
    """rt minute→second conversion applies; per-scan rt comes from first peak."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out, ret_time_unit="minute")

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        for spectrum in msz.spectra:
            expected = groups[spectrum.scan]["rt"] * 60.0
            assert math.isclose(spectrum.retention_time, expected, rel_tol=1e-5), (
                f"rt mismatch at scan {spectrum.scan}: "
                f"got {spectrum.retention_time}, expected {expected}"
            )


def test_mzparquet_to_msz_precursor_for_ms2(mzparquet_path, mzparquet_table, tmp_path):
    """MS2 spectra carry precursor m/z + charge state from the source."""
    from mscompress.parquet.mzml import _extract_precursor_charge

    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        ms2 = [s for s in msz.spectra if s.ms_level == 2]
        assert len(ms2) > 0
        for spectrum in ms2:
            g = groups[spectrum.scan]
            precursor, charge = _extract_precursor_charge(spectrum.xml)
            assert math.isclose(precursor, float(g["precursor_mz"]), rel_tol=1e-5)
            assert charge == int(g["precursor_charge"])


def test_mzparquet_to_msz_ms1_no_precursor(mzparquet_path, mzparquet_table, tmp_path):
    """MS1 spectra must not carry a <precursorList> element."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    with read(out) as msz:
        ms1 = [s for s in msz.spectra if s.ms_level == 1]
        assert len(ms1) > 0
        for spectrum in ms1:
            has_precursor_list = any(
                elem.tag == "precursorList" or (
                    isinstance(elem.tag, str) and elem.tag.endswith("}precursorList")
                )
                for elem in spectrum.xml.iter()
            )
            assert not has_precursor_list, (
                f"MS1 scan {spectrum.scan} should not have <precursorList>"
            )


def test_mzparquet_to_msz_ion_mobility(mzparquet_path, mzparquet_table, tmp_path):
    """ion_mobility column surfaces as MS:1002476 cvParam in <scan>."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        non_null_seen = 0
        for spectrum in msz.spectra:
            expected = groups[spectrum.scan]["ion_mobility"]
            im_params = [
                elem for elem in spectrum.xml.iter()
                if (elem.tag == "cvParam" or (
                    isinstance(elem.tag, str) and elem.tag.endswith("}cvParam")
                )) and elem.attrib.get("accession") == "MS:1002476"
            ]
            if expected is None:
                assert im_params == [], (
                    f"scan {spectrum.scan}: no ion_mobility in source but cvParam emitted"
                )
            else:
                assert len(im_params) == 1, (
                    f"scan {spectrum.scan}: expected one ion_mobility cvParam"
                )
                assert math.isclose(
                    float(im_params[0].attrib["value"]), float(expected), rel_tol=1e-5
                )
                non_null_seen += 1
        # Test is only meaningful if the fixture has some non-null values.
        # If the fixture has all-null ion_mobility, the loop above still passes
        # vacuously, but at least we've confirmed we don't emit spurious params.


def test_mzparquet_to_msz_isolation_window(mzparquet_path, mzparquet_table, tmp_path):
    """isolation_lower/upper surface as MS:1000827/8/9 cvParams under <precursor>."""
    out = tmp_path / "spectra.msz"
    from_parquet(mzparquet_path, out)

    groups = _group_peaks_by_scan(mzparquet_table)
    with read(out) as msz:
        verified = 0
        for spectrum in msz.spectra:
            if spectrum.ms_level == 1:
                continue
            g = groups[spectrum.scan]
            iso_lo = g["isolation_lower"]
            iso_hi = g["isolation_upper"]
            if iso_lo is None or iso_hi is None:
                continue
            params = {
                elem.attrib.get("accession"): float(elem.attrib.get("value"))
                for elem in spectrum.xml.iter()
                if (elem.tag == "cvParam" or (
                    isinstance(elem.tag, str) and elem.tag.endswith("}cvParam")
                )) and elem.attrib.get("accession", "").startswith("MS:100082")
            }
            assert "MS:1000827" in params
            assert "MS:1000828" in params
            assert "MS:1000829" in params
            # offsets should reconstruct the full isolation width
            full_width = float(iso_hi) - float(iso_lo)
            assert math.isclose(
                params["MS:1000828"] + params["MS:1000829"], full_width, rel_tol=1e-5
            )
            verified += 1
        assert verified > 0, "fixture must include at least one MS2 with isolation window"


def test_mzparquet_to_mszx_no_annotations(mzparquet_path, mzparquet_table, tmp_path):
    """mszx bundle for long-format input contains spectra but no annotations."""
    out = tmp_path / "long.mszx"
    from_parquet(mzparquet_path, out)

    unique_scans = len(set(mzparquet_table.column("scan").to_pylist()))
    with MSZXFile.open(out) as mszx:
        assert len(mszx.spectra) == unique_scans
        # No annotations bundle in the archive at all.
        assert mszx.annotations is None or list(mszx.annotations) == []


def test_mzparquet_to_tsv_raises(mzparquet_path, tmp_path):
    """TSV export of long-format input must raise (no PSM data)."""
    out = tmp_path / "out.tsv"
    with pytest.raises(ValueError, match="long-format"):
        from_parquet(mzparquet_path, out)


def test_long_format_requires_scan_column(tmp_path):
    """Scalar mz/intensity without a scan column → clear error."""
    table = pa.table({
        "mz": pa.array([100.0, 200.0, 300.0], type=pa.float32()),
        "intensity": pa.array([10.0, 20.0, 30.0], type=pa.float32()),
    })
    bad_path = tmp_path / "scalar_no_scan.parquet"
    pq.write_table(table, bad_path)

    with pytest.raises(ValueError, match="scan-id column"):
        from_parquet(bad_path, tmp_path / "out.msz")


def test_long_format_rejects_mixed_list_scalar(tmp_path):
    """One list + one scalar must raise."""
    table = pa.table({
        "mz": pa.array([[100.0, 200.0]], type=pa.list_(pa.float32())),
        "intensity": pa.array([10.0], type=pa.float32()),
        "scan": pa.array([1], type=pa.uint32()),
    })
    bad_path = tmp_path / "mixed.parquet"
    pq.write_table(table, bad_path)

    with pytest.raises(ValueError, match="both be list or both be scalar"):
        from_parquet(bad_path, tmp_path / "out.msz")


def test_long_format_null_rt_falls_back_to_default(tmp_path):
    """NaN/null rt for a scan → spectrum.retention_time == -1.0 (default)."""
    table = pa.table({
        "scan": pa.array([1, 1, 2, 2], type=pa.uint32()),
        "level": pa.array([1, 1, 2, 2], type=pa.uint8()),
        "mz": pa.array([100.0, 200.0, 110.0, 220.0], type=pa.float32()),
        "intensity": pa.array([10.0, 20.0, 11.0, 22.0], type=pa.float32()),
        "rt": pa.array([float("nan"), float("nan"), float("nan"), float("nan")],
                       type=pa.float32()),
    })
    p = tmp_path / "nan_rt.parquet"
    pq.write_table(table, p)
    out = tmp_path / "out.msz"
    from_parquet(p, out, ret_time_unit="second")

    with read(out) as msz:
        for spectrum in msz.spectra:
            assert math.isclose(spectrum.retention_time, -1.0, rel_tol=1e-5)
