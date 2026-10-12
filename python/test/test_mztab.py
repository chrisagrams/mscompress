"""Tests for mzTab as the single stored search-results format."""

import dataclasses
import json
import math
import shutil
import tarfile
from pathlib import Path

import pytest
import zstandard as zstd

from mscompress import MSZXBatchWriter, read
from mscompress.annotations import (
    PSM,
    ModificationSpec,
    MzTabReader,
    PSMReader,
    SearchInfo,
    reader_to_mztab,
    write_mztab,
)
from mscompress.mszx import MSZXBatchFile, MSZXFile, create_mszx, upgrade_mszx
from mscompress.types import AnnotationFormat

_MZTAB_DIR = Path(__file__).resolve().parents[2] / "test" / "data" / "mztab"


def _as_dicts(psms):
    return [dataclasses.asdict(p) for p in psms]


def _load_expected(name):
    """Expected PSMs/search info, shared with the Node tests."""
    doc = json.loads((_MZTAB_DIR / f"{name}.psms.json").read_text())

    def restore(v):
        if v in ("nan", "inf", "-inf"):
            return float(v)
        if isinstance(v, dict):
            return {k: restore(x) for k, x in v.items()}
        if isinstance(v, list):
            return [restore(x) for x in v]
        return v

    return restore(doc)


def _assert_psms_equal(actual, expected):
    assert len(actual) == len(expected)
    for a, e in zip(actual, expected):
        assert a.keys() == e.keys()
        for key in a:
            av, ev = a[key], e[key]
            if isinstance(ev, float) and math.isnan(ev):
                assert math.isnan(av), key
            else:
                assert av == ev, (key, av, ev)


def _mztab_reader(text, tmp_path, name="x.mztab"):
    path = tmp_path / name
    path.write_text(text)
    return MzTabReader(path)


# -- writer layout ---------------------------------------------------------


def test_writer_layout_and_protein_rows(tmp_path):
    psms = [
        PSM(scan_number=7, peptide="PEPTIDE", charge=2, score=1.5,
            proteins=["P1", "P2"], q_value=0.01, retention_time=12.5,
            extra={"delta score": 3.0, "label": "x"}),
        PSM(scan_number=9, peptide="OTHER", charge=3, score=0.5,
            spectrum_index=4, is_decoy=True),
    ]
    text = write_mztab(psms, ms_run_location="run.msz",
                       source_format="pepxml", source_name="run.pepXML")
    lines = text.splitlines()

    assert lines[:3] == [
        "MTD\tmzTab-version\t1.0.0",
        "MTD\tmzTab-mode\tSummary",
        "MTD\tmzTab-type\tIdentification",
    ]
    assert "MTD\tms_run[1]-location\tfile://run.msz" in lines
    assert "MTD\tfixed_mod[1]\t[MS, MS:1002453, No fixed modifications searched, ]" in lines

    header = next(l for l in lines if l.startswith("PSH\t")).split("\t")[1:]
    assert header[:3] == ["sequence", "PSM_ID", "accession"]
    assert header[-2:] == ["opt_global_extra_delta_score", "opt_global_extra_label"]

    rows = [dict(zip(header, l.split("\t")[1:])) for l in lines if l.startswith("PSM\t")]
    # One row per protein, sharing PSM_ID; a protein-less PSM gets one null row.
    assert [(r["PSM_ID"], r["accession"], r["unique"]) for r in rows] == [
        ("1", "P1", "0"), ("1", "P2", "0"), ("2", "null", "null"),
    ]
    assert rows[0]["spectra_ref"] == "ms_run[1]:scan=7"
    assert rows[2]["spectra_ref"] == "ms_run[1]:index=4"
    assert rows[2]["search_engine_score[2]"] == "null"
    assert rows[2]["opt_global_cv_MS:1002217_decoy_peptide"] == "1"
    assert rows[2]["opt_global_extra_label"] == "null"

    back = _mztab_reader(text, tmp_path)
    assert back[0].proteins == ["P1", "P2"]
    assert back[0].extra == {"delta_score": 3.0, "label": "x"}
    assert back[1].spectrum_index == 4 and back[1].is_decoy
    assert back.source_format == "pepxml" and back.source_name == "run.pepXML"


def test_search_info_round_trip(tmp_path):
    info = SearchInfo(
        software="Comet", software_version="2024.01", primary_score_name="xcorr",
        fixed_mods=[ModificationSpec(57.021464, "C")],
        variable_mods=[ModificationSpec(-17.026549, "N-term", "Any N-term")],
    )
    psm = PSM(scan_number=1, peptide="A", charge=1, score=2.0)
    back = _mztab_reader(write_mztab([psm], search_info=info), tmp_path)
    assert back.search_info == info
    assert "[CHEMMOD, CHEMMOD:-17.026549, , ]" in back.metadata["variable_mod[1]"]


def test_reads_third_party_mztab(tmp_path):
    text = "\n".join([
        "COM\tmade elsewhere",
        "MTD\tmzTab-version\t1.0.0",
        "MTD\tpsm_search_engine_score[1]\t[MS, MS:1002354, PSM-level q-value, ]",
        "MTD\tpsm_search_engine_score[2]\t[MS, MS:1001330, X!Tandem:expect, ]",
        "",
        "PSH\tsequence\tPSM_ID\taccession\tcharge\tretention_time\tspectra_ref"
        "\tsearch_engine_score[1]\tsearch_engine_score[2]\topt_global_decoy\topt_ms_run[1]_foo",
        "PSM\tAAK\t1\tP1\t2\t10.5|11.0\tms_run[1]:controllerType=0 controllerNumber=1 scan=42"
        "\t0.001\t1e-5\ttrue\tbar",
        "PRH\taccession",
        "PRT\tP1",
    ]) + "\n"
    reader = _mztab_reader(text, tmp_path)
    assert len(reader) == 1
    psm = reader[0]
    assert (psm.scan_number, psm.charge, psm.retention_time) == (42, 2, 10.5)
    assert psm.q_value == 0.001 and psm.score == 1e-5 and psm.pep is None
    assert psm.is_decoy
    assert psm.extra == {"opt_ms_run[1]_foo": "bar"}
    assert reader.search_info.primary_score_name == "expect"


# -- conversion (shared fixtures with the Node tests) ----------------------


@pytest.mark.parametrize("source", ["pepXML/test.pepXML", "percolator/test.tsv"])
def test_conversion_matches_shared_fixtures(test_data_dir, source, tmp_path):
    name = Path(source).name
    expected = _load_expected(name)

    reader = PSMReader(Path(test_data_dir) / source)
    _assert_psms_equal(_as_dicts(reader), expected["psms"])
    assert dataclasses.asdict(reader.search_info) == expected["search_info"]

    # Converted here, then read back.
    back = _mztab_reader(reader_to_mztab(reader, ms_run_location="test.msz"), tmp_path)
    _assert_psms_equal(_as_dicts(back), expected["psms"])
    assert dataclasses.asdict(back.search_info) == expected["search_info"]

    # The checked-in mzTab, which the Node tests read too.
    fixture = MzTabReader(_MZTAB_DIR / f"{name}.mztab")
    _assert_psms_equal(_as_dicts(fixture), expected["psms"])


def test_percolator_scores_promoted(percolator_tsv_file_path):
    reader = PSMReader(percolator_tsv_file_path)
    psm = reader[0]
    assert psm.q_value == pytest.approx(0.277778)
    assert psm.pep == pytest.approx(0.328432)
    # Flanks are stripped without cutting at the dot inside a mass shift.
    assert "EVM[15.9949]SLHEASR" in {p.peptide for p in reader}


def test_unknown_software_round_trips_as_none(tmp_path):
    psm = PSM(scan_number=1, peptide="A", charge=1, score=1.0)
    assert _mztab_reader(write_mztab([psm]), tmp_path).search_info.software is None


def test_filter_keeps_header_and_protein_rows(tmp_path):
    psms = [
        PSM(scan_number=s, peptide="PEP", charge=2, score=1.0, proteins=["A", "B"])
        for s in (1, 2, 3)
    ]
    reader = _mztab_reader(write_mztab(psms), tmp_path)
    out = tmp_path / "subset.mztab"
    reader.filter_to_file(out, {2})

    subset = MzTabReader(out)
    assert [p.scan_number for p in subset] == [2]
    assert subset[0].proteins == ["A", "B"]
    assert subset.metadata == reader.metadata


# -- archives --------------------------------------------------------------


def test_builder_stores_only_mztab(tmp_path, msz_file_path, pepxml_file_path,
                                   percolator_tsv_file_path):
    out = tmp_path / "out.mszx"
    with read(msz_file_path) as msz:
        create_mszx(msz, out, annotations=[pepxml_file_path, percolator_tsv_file_path])

    with tarfile.open(out) as tar:
        names = sorted(tar.getnames())
    assert names == ["manifest.json", "test.msz", "test.pepXML.mztab.zst", "test.tsv.mztab.zst"]

    with MSZXFile.open(out) as mszx:
        assert mszx.manifest.join_key == "spectra_ref"
        assert {e.format for e in mszx.annotation_files} == {AnnotationFormat.MZTAB}
        assert [e.num_records for e in mszx.annotation_files] == [100, 100]
        assert isinstance(mszx.annotations, MzTabReader)
        reader = mszx.get_annotation_reader("test.pepXML.mztab.zst")
        assert reader.source_format == "pepxml"
        _assert_psms_equal(_as_dicts(reader), _load_expected("test.pepXML")["psms"])


def test_mztab_input_is_stored_unchanged(tmp_path, msz_file_path):
    src = _MZTAB_DIR / "test.tsv.mztab"
    out = tmp_path / "out.mszx"
    with read(msz_file_path) as msz:
        create_mszx(msz, out, annotations=[str(src)])
    with tarfile.open(out) as tar:
        stored = tar.extractfile("test.tsv.mztab.zst").read()
    assert zstd.ZstdDecompressor().decompress(stored) == src.read_bytes()


def test_legacy_archive_still_reads(mszx_file_path):
    with MSZXFile.open(mszx_file_path) as mszx:
        assert mszx.annotation_files[0].format == AnnotationFormat.PEPXML
        assert len(mszx.annotations) == 100


def test_upgrade_legacy_single_file_archive(tmp_path, mszx_file_path):
    with MSZXFile.open(mszx_file_path) as legacy:
        expected = _as_dicts(legacy.annotations)
        n_spectra = len(legacy.spectra)

    out = upgrade_mszx(mszx_file_path, tmp_path / "upgraded.mszx")

    with tarfile.open(out) as tar:
        assert "HSA.pepXML.mztab.zst" in tar.getnames()
        assert "HSA.pepXML.zst" not in tar.getnames()
    with MSZXFile.open(out) as mszx:
        entry = mszx.annotation_files[0]
        assert (entry.filename, entry.format) == ("HSA.pepXML.mztab.zst", AnnotationFormat.MZTAB)
        assert len(mszx.spectra) == n_spectra
        assert isinstance(mszx.annotations, MzTabReader)
        assert mszx.annotations.source_name == "HSA.pepXML"
        _assert_psms_equal(_as_dicts(mszx.annotations), expected)


def test_upgrade_in_place_is_idempotent(tmp_path, mszx_file_path):
    target = tmp_path / "copy.mszx"
    shutil.copy(mszx_file_path, target)
    upgrade_mszx(target)
    first = target.read_bytes()
    upgrade_mszx(target)
    assert target.read_bytes() == first


def test_batch_writer_add_psms_and_upgrade(tmp_path, mzml_file_path, pepxml_file_path):
    out = tmp_path / "batch.mszx"
    with MSZXBatchWriter(out) as writer:
        idx = writer.add(mzml_file_path)
        member = writer.add_psms(idx, pepxml_file_path)
        assert member == "test.pepXML.mztab.zst"
        # A legacy-style raw member, as older writers produced.
        writer.add_annotation(idx, Path(pepxml_file_path).read_bytes(),
                              "legacy.pepXML", format="pepxml")

    upgraded = upgrade_mszx(out, tmp_path / "batch_upgraded.mszx")
    with MSZXBatchFile.open(upgraded) as archive:
        anns = archive.entries[0].annotations
        assert [(a.filename, a.format) for a in anns] == [
            ("test.pepXML.mztab.zst", AnnotationFormat.MZTAB),
            ("legacy.pepXML.mztab", AnnotationFormat.MZTAB),
        ]
        assert archive.entries[0].join_key == "spectra_ref"
        assert len(archive[0].spectra) > 0

        stored = zstd.ZstdDecompressor().decompress(
            archive.get_annotation(0, "test.pepXML.mztab.zst"))
        text = archive.get_annotation(0, "legacy.pepXML.mztab").decode()
        assert "MTD\tms_run[1]-location\tfile://test.mzML" in text
        a = _mztab_reader(stored.decode(), tmp_path, "a.mztab")
        b = _mztab_reader(text, tmp_path, "b.mztab")
        _assert_psms_equal(_as_dicts(a), _as_dicts(b))
