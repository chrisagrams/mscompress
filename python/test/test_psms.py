"""Tests for PSMTable, the search-results readers, and mzTab storage."""

import math
import tarfile
import warnings
from pathlib import Path

import pandas as pd
import pytest
import zstandard as zstd

from mscompress import MSZXBatchWriter, read
from mscompress.annotations import (
    PSM_COLUMNS,
    PSM_SCHEMA,
    ModificationSpec,
    MzTab,
    PepXML,
    Percolator,
    PSMTable,
    SearchMetadata,
    SearchResultsFormat,
    psm_frame,
    read_psms,
)
from mscompress.mszx import MSZXBatchFile, MSZXBuilder, MSZXFile, create_mszx
from mscompress.types import AnnotationFormat

_MZTAB_DIR = Path(__file__).resolve().parents[2] / "test" / "data" / "mztab"


def _table(rows, metadata=None):
    """A hand-built table."""
    return PSMTable(psm_frame(pd.DataFrame(rows)), metadata)


def _rows(text, tag="PSM"):
    lines = text.splitlines()
    header = next(l for l in lines if l.startswith("PSH\t")).split("\t")[1:]
    return [dict(zip(header, l.split("\t")[1:])) for l in lines if l.startswith(tag + "\t")]


def _assert_metadata_close(actual, expected):
    """UNIMOD-mapped mods read back with UNIMOD's mass, not the engine's."""
    for kind in ("fixed_mods", "variable_mods"):
        a, e = getattr(actual, kind), getattr(expected, kind)
        assert [(m.site, m.position) for m in a] == [(m.site, m.position) for m in e]
        assert all(math.isclose(x.massdiff, y.massdiff, abs_tol=1e-3) for x, y in zip(a, e))
    for field in ("software", "software_version", "primary_score",
                  "ms_run_location", "source_format", "source_file"):
        assert getattr(actual, field) == getattr(expected, field), field


# -- readers ---------------------------------------------------------------


def test_pepxml_reader(pepxml_file_path):
    table = PepXML().read(pepxml_file_path)
    df = table.df
    assert len(df) == 100
    assert list(df.columns[: len(PSM_COLUMNS)]) == PSM_COLUMNS
    assert (df.scan[0], df.peptide[0], df.charge[0]) == (493, "KSHHANSPTAGAAK", 3)

    md = table.metadata
    # MSFragger names itself in the version and claims to be X! Tandem.
    assert (md.software, md.software_version) == ("MSFragger", "4.0")
    assert md.primary_score == "hyperscore"
    assert md.source_format == "pepxml" and md.source_file == "test.pepXML"
    assert md.fixed_mods == [ModificationSpec(57.021465, "C")]
    assert df.score.equals(df.hyperscore)

    first = df.iloc[0]
    assert math.isclose(first.precursor_mz, (1375.6925 + 3 * 1.007276) / 3)
    assert (first.pre, first.post) == ("K", "S")
    # Decoys use the file's own decoy_prefix ("rev_").
    assert df.is_decoy.sum() == 22
    assert all(p[0].startswith("rev_") for p in df.proteins[df.is_decoy])


def test_pepxml_modifications(pepxml_file_path):
    df = PepXML().read(pepxml_file_path).df
    mods = df.groupby("modified_peptide").modifications.unique()
    # Variable mods from mod_aminoacid_mass, fixed C mods added from the summary.
    assert list(mods["M[147]TAM[147]DNASK"]) == ["1-UNIMOD:35,4-UNIMOD:35"]
    assert list(mods["KQRYCSSADK"]) == ["5-UNIMOD:4"]
    assert df.modifications.notna().sum() == 56


def test_percolator_reader(percolator_tsv_file_path):
    table = Percolator().read(percolator_tsv_file_path)
    df = table.df
    assert len(df) == 100
    assert (df.scan[0], df.peptide[0]) == (4310, "TDLNPDNLQGGDDLDPNYVLSSR")
    assert math.isclose(df.q_value[0], 0.277778) and math.isclose(df.pep[0], 0.328432)
    assert df.proteins[1] == ["sp|O95922-4|TTLL1_HUMAN", "sp|O95922|TTLL1_HUMAN"]

    # The bracketed mass stays in modified_peptide, not in peptide.
    row = df[df.modified_peptide == "EVM[15.9949]SLHEASR"].iloc[0]
    assert (row.peptide, row.modifications, row.pre, row.post) == (
        "EVMSLHEASR", "3-UNIMOD:35", "R", "K")

    md = table.metadata
    assert (md.software, md.primary_score) == ("Percolator", "percolator:score")
    # Percolator doesn't record the searched mods; the observed ones are declared.
    assert [(m.site, m.massdiff) for m in md.variable_mods] == [("M", 15.9949), ("C", 57.0215)]


def test_pin_reader_handles_labels_and_spilled_proteins():
    pin = (
        "SpecId\tLabel\tScanNr\tExpMass\tPeptide\tProteins\n"
        "run.7.7.2_1\t1\t7\t1000.5\tK.n[42.0106]PEPC[57.0215]K.L\tP1\tP2\n"
        "run.8.8.3_1\t-1\t8\t999.1\t-.ABC.-\tDECOY_P3\n"
    )
    df = Percolator().read(pin.encode()).df
    assert df.scan.tolist() == [7, 8] and df.charge.tolist() == [2, 3]
    assert df.is_decoy.tolist() == [False, True]
    assert df.proteins[0] == ["P1", "P2"]
    assert df.peptide[0] == "PEPCK"
    assert df.modifications[0] == "0-UNIMOD:1,4-UNIMOD:4"
    assert df.ExpMass.tolist() == [1000.5, 999.1]
    assert df.score.isna().all()


def test_formats_share_the_base_class():
    for fmt in (MzTab, PepXML, Percolator):
        assert issubclass(fmt, SearchResultsFormat)
    # Search results are only ever stored as mzTab.
    assert hasattr(MzTab, "write")
    assert not hasattr(PepXML, "write") and not hasattr(Percolator, "write")


def test_reader_options(pepxml_file_path):
    # The file declares decoy_prefix "rev_"; an explicit prefix wins.
    assert PepXML(decoy_prefix="sp|").read(pepxml_file_path).df.is_decoy.sum() == 78
    assert len(PepXML(max_rank=0).read(pepxml_file_path)) == 0


def test_read_psms_detects_formats(pepxml_file_path, percolator_tsv_file_path):
    pepxml = Path(pepxml_file_path).read_bytes()
    assert len(read_psms(zstd.ZstdCompressor().compress(pepxml))) == 100
    assert read_psms(percolator_tsv_file_path).metadata.software == "Percolator"
    assert len(read_psms(_MZTAB_DIR / "test.tsv.mztab")) == 100
    with pytest.raises(ValueError):
        read_psms(b"not search results")


# -- mzTab -----------------------------------------------------------------


@pytest.mark.parametrize("source", ["pepXML/test.pepXML", "percolator/test.tsv"])
def test_conversion_matches_checked_in_mztab(test_data_dir, source, tmp_path):
    table = read_psms(Path(test_data_dir) / source)
    text = MzTab().write(table, ms_run_location="test.mzML")
    assert text == (_MZTAB_DIR / f"{Path(source).name}.mztab").read_text()


@pytest.mark.parametrize("source", ["pepXML/test.pepXML", "percolator/test.tsv"])
def test_mztab_round_trip(test_data_dir, source):
    table = read_psms(Path(test_data_dir) / source)
    back = MzTab().read(MzTab().write(table).encode())
    pd.testing.assert_frame_equal(back.df, table.df, check_dtype=False)
    _assert_metadata_close(back.metadata, table.metadata)


def test_writer_layout():
    table = _table([
        {"scan": 7, "peptide": "PEPTIDE", "charge": 2, "score": 1.5,
         "proteins": ["P1", "P2"], "q_value": 0.01, "retention_time": 12.5,
         "is_decoy": False, "delta score": 3.0, "label": "x"},
        {"scan": 9, "peptide": "OTHER", "charge": 3, "score": math.inf,
         "proteins": [], "is_decoy": True},
    ], SearchMetadata(software="Comet", software_version="2024.01", primary_score="xcorr"))
    text = MzTab().write(table, ms_run_location="run.msz")
    lines = text.splitlines()

    assert lines[:3] == [
        "MTD\tmzTab-version\t1.0.0",
        "MTD\tmzTab-mode\tSummary",
        "MTD\tmzTab-type\tIdentification",
    ]
    assert "MTD\tms_run[1]-location\tfile://run.msz" in lines
    assert "MTD\tsoftware[1]\t[MS, MS:1002251, Comet, 2024.01]" in lines
    assert "MTD\tpsm_search_engine_score[1]\t[MS, MS:1001155, SEQUEST:xcorr, ]" in lines
    assert "MTD\tfixed_mod[1]\t[MS, MS:1002453, No fixed modifications searched, ]" in lines

    rows = _rows(text)
    # One row per protein, sharing PSM_ID; a protein-less PSM keeps one null row.
    assert [(r["PSM_ID"], r["accession"], r["unique"]) for r in rows] == [
        ("1", "P1", "0"), ("1", "P2", "0"), ("2", "null", "null"),
    ]
    assert rows[0]["spectra_ref"] == "ms_run[1]:scan=7"
    assert rows[0]["opt_global_delta_score"] == "3.0"
    assert rows[2]["search_engine_score[1]"] == "INF"
    assert rows[2]["opt_global_cv_MS:1002217_decoy_peptide"] == "1"
    assert rows[2]["opt_global_label"] == "null"

    back = MzTab().read(text.encode())
    assert back.df.proteins[0] == ["P1", "P2"]
    assert back.df.score[1] == math.inf
    assert back.df.delta_score[0] == 3.0 and back.df.label[0] == "x"


def test_mod_params_in_metadata():
    md = SearchMetadata(
        fixed_mods=[ModificationSpec(57.021464, "C")],
        variable_mods=[ModificationSpec(-17.026549, "N-term", "Any N-term"),
                       ModificationSpec(12.5, "K")],
    )
    text = MzTab().write(PSMTable(None, md))
    assert "MTD\tfixed_mod[1]\t[UNIMOD, UNIMOD:4, Carbamidomethyl, ]" in text
    # Shifts without a UNIMOD entry in the table are written as CHEMMOD.
    assert "MTD\tvariable_mod[1]\t[CHEMMOD, CHEMMOD:-17.026549, CHEMMOD:-17.026549, ]" in text
    assert "MTD\tvariable_mod[2]\t[CHEMMOD, CHEMMOD:+12.5, CHEMMOD:+12.5, ]" in text
    assert MzTab().read(text.encode()).metadata == md


def test_unknown_software_round_trips_as_none():
    text = MzTab().write(_table([{"scan": 1, "peptide": "A"}]))
    assert "MTD\tsoftware[1]\t[MS, MS:1001456, analysis software, ]" in text
    assert MzTab().read(text.encode()).metadata.software is None


# -- PSMTable ---------------------------------------------------------------


def test_table_lookup_and_filter():
    table = _table([
        {"scan": 5, "peptide": "A", "score": 1.0},
        {"scan": 3, "peptide": "B", "score": 2.0},
        {"scan": 5, "peptide": "C", "score": 3.0},
    ])
    assert table.scans == [3, 5]
    assert table.has_scan(5) and not table.has_scan(4)
    assert table.for_scan(5).peptide.tolist() == ["A", "C"]
    assert [r["peptide"] for r in table.records_for_scan(5)] == ["A", "C"]
    assert table.records_for_scan(4) == []
    assert table.filter_scans({3}).df.peptide.tolist() == ["B"]
    assert len(PSMTable()) == 0
    # Core columns the rows don't have are empty: NA, no proteins, not a decoy.
    assert table.df.proteins.tolist() == [[], [], []]
    assert table.df.is_decoy.tolist() == [False] * 3


@pytest.mark.parametrize("source", ["pepXML/test.pepXML", "percolator/test.tsv",
                                    "mztab/test.pepXML.mztab"])
def test_readers_produce_the_schema(test_data_dir, source):
    df = read_psms(Path(test_data_dir) / source).df
    assert {c: str(t) for c, t in df.dtypes.iloc[:len(PSM_SCHEMA)].items()} == PSM_SCHEMA


# -- archives --------------------------------------------------------------


def test_builder_stores_only_mztab(tmp_path, msz_file_path, pepxml_file_path,
                                   percolator_tsv_file_path):
    out = tmp_path / "out.mszx"
    with read(msz_file_path) as msz:
        create_mszx(msz, out, psms=[pepxml_file_path, percolator_tsv_file_path])

    with tarfile.open(out) as tar:
        names = sorted(tar.getnames())
    assert names == ["manifest.json", "test.msz", "test.pepXML.mztab.zst", "test.tsv.mztab.zst"]

    with MSZXFile.open(out) as mszx:
        assert mszx.manifest.join_key == "spectra_ref"
        assert {e.format for e in mszx.annotation_files} == {AnnotationFormat.MZTAB}
        assert [e.num_records for e in mszx.annotation_files] == [100, 100]
        assert list(mszx.psm_tables) == ["test.pepXML.mztab.zst", "test.tsv.mztab.zst"]
        table = mszx.get_psms("test.pepXML.mztab.zst")
        assert mszx.psms is table
        assert table.metadata.ms_run_location == "test.msz"
        assert table.metadata.source_format == "pepxml"
        pd.testing.assert_frame_equal(table.df, PepXML().read(pepxml_file_path).df,
                                      check_dtype=False)


def test_builder_names_and_duplicates(tmp_path, msz_file_path):
    with read(msz_file_path) as msz:
        builder = MSZXBuilder(msz, compression=False)
        builder.add_psms(_MZTAB_DIR / "test.tsv.mztab")
        builder.add_psms(_table([{"scan": 1, "peptide": "A"}]), name="mine")
        with pytest.raises(ValueError, match="Duplicate"):
            builder.add_psms(PSMTable(), name="mine.mztab")
        out = builder.save(tmp_path / "out.mszx")

    with MSZXFile.open(out) as mszx:
        assert list(mszx.psm_tables) == ["test.tsv.mztab", "mine.mztab"]
        assert len(mszx.get_psms("mine.mztab")) == 1


def test_batch_writer_add_psms(tmp_path, mzml_file_path, pepxml_file_path):
    out = tmp_path / "batch.mszx"
    with MSZXBatchWriter(out) as writer:
        idx = writer.add(mzml_file_path)
        assert writer.add_psms(idx, pepxml_file_path) == "test.pepXML.mztab.zst"

    with MSZXBatchFile.open(out) as archive:
        anns = archive.entries[0].annotations
        assert [(a.filename, a.format, a.num_records) for a in anns] == [
            ("test.pepXML.mztab.zst", AnnotationFormat.MZTAB, 100),
        ]
        table = archive.get_psms(0, "test.pepXML.mztab.zst")
        assert table.metadata.ms_run_location == "test.mzML"
        assert len(table) == 100


def test_legacy_annotation_members_are_skipped(legacy_mszx_file_path):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        mszx = MSZXFile.open(legacy_mszx_file_path)
    try:
        assert any("only mzTab" in str(w.message) for w in caught)
        assert mszx.psms is None and mszx.psm_tables == {}
        assert len(mszx.spectra) > 0
    finally:
        mszx.close()
