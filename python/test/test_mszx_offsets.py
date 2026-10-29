"""Member byte offsets in the v2 batch manifest.

Each member's ``offset`` is the absolute position of its payload, so a reader
can open it without walking the tar headers. Readers check the header at
``offset - 512`` first and fall back to the walk when it doesn't match.
"""

import copy
import shutil
import tarfile
from pathlib import Path
from typing import Any, List

import pytest

from mscompress import MSZXBatchWriter
from mscompress.mszx.batch import MSZXBatchFile, _checked_span

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
TEST_MZML = REPO_ROOT / "test" / "data" / "test.mzML"

ANNOTATION = b"scan\tpeptide\n1\tPEPTIDE\n"


@pytest.fixture(scope="module")
def archive_path(tmp_path_factory):
    """Two members, an annotation on the second, and the index."""
    work = tmp_path_factory.mktemp("offsets")
    shutil.copy(TEST_MZML, work / "a.mzML")
    shutil.copy(TEST_MZML, work / "b.mzML")
    out = work / "offsets.mszx"
    with MSZXBatchWriter(out) as writer:
        writer.add(work / "a.mzML")
        idx = writer.add(work / "b.mzML")
        writer.add_annotation(idx, ANNOTATION, "annotations/b.tsv", format="tsv")
    return out


def _tar_spans(path):
    with tarfile.open(path, "r") as tar:
        return {m.name: (m.offset_data, m.size) for m in tar.getmembers()}


def test_every_member_records_its_payload_offset(archive_path):
    spans = _tar_spans(archive_path)
    with MSZXBatchFile.open(archive_path) as archive:
        manifest = archive.manifest
        for entry in manifest.spectra_files:
            assert (entry.offset, entry.size) == spans[entry.entry]
        ann = manifest.spectra_files[1].annotations[0]
        assert (ann.offset, ann.size) == spans[ann.filename]
        assert ann.size == len(ANNOTATION)
        index = manifest.index
        assert index is not None
        assert (index.offset, index.size) == spans[index.entry]


def test_offsets_survive_a_manifest_round_trip(archive_path):
    with MSZXBatchFile.open(archive_path) as archive:
        data = archive.manifest.to_dict()
    assert "offset" in data["spectra_files"][0]
    assert "offset" in data["spectra_files"][1]["annotations"][0]
    assert "offset" in data["index"]


def test_checked_span_rejects_a_mismatched_header(archive_path):
    spans = _tar_spans(archive_path)
    offset, size = spans["a.msz"]
    assert _checked_span(archive_path, "a.msz", offset, size) == (offset, size)
    assert _checked_span(archive_path, "b.msz", offset, size) is None
    assert _checked_span(archive_path, "a.msz", offset, size + 1) is None
    assert _checked_span(archive_path, "a.msz", offset + 512, size) is None
    assert _checked_span(archive_path, "a.msz", None, size) is None


def _read_everything(archive):
    return (
        [len(m.spectra) for m in archive],
        archive.get_annotation(1, "annotations/b.tsv"),
        len(archive.index),
    )


@pytest.mark.parametrize("mode", ["missing", "wrong"])
def test_reader_falls_back_to_the_tar_walk(archive_path, mode):
    """A manifest without offsets, or with bad ones, still reads the same."""
    with MSZXBatchFile.open(archive_path) as archive:
        expected = _read_everything(archive)
        manifest = copy.deepcopy(archive.manifest)

    assert manifest.index is not None
    records: List[Any] = [
        *manifest.spectra_files,
        manifest.index,
        *manifest.spectra_files[1].annotations,
    ]
    for rec in records:
        rec.offset = None if mode == "missing" else rec.offset + 512

    with MSZXBatchFile(archive_path, manifest) as archive:
        assert _read_everything(archive) == expected


def test_valid_offsets_skip_the_tar_walk(archive_path, monkeypatch):
    with MSZXBatchFile.open(archive_path) as archive:
        expected = _read_everything(archive)
    archive = MSZXBatchFile.open(archive_path)

    def no_walk(*args, **kwargs):
        raise AssertionError("tar headers were walked despite valid offsets")

    monkeypatch.setattr(tarfile, "open", no_walk)
    with archive:
        assert _read_everything(archive) == expected
