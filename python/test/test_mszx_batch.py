"""Reading v2 multi-file ("batch") MSZX archives.

The archives here are built by the C CLI when it is available, so these double
as cross-language conformance tests: whatever the shared C batch writer emits
must be readable by the Python binding.
"""

import json
import shutil
import subprocess
import tarfile
from pathlib import Path

import pytest

import mscompress
from mscompress.mszx.batch import MSZXBatchFile
from mscompress.mszx.metadata import MSZXBatchManifest

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CLI = REPO_ROOT / "cli" / "mscompress"
TEST_MZML = REPO_ROOT / "test" / "data" / "test.mzML"

requires_cli = pytest.mark.skipif(
    not CLI.exists(), reason="C CLI not built; run cmake --build . first"
)


@pytest.fixture(scope="module")
def batch_archive(tmp_path_factory):
    """A two-member .mszx produced by the C CLI."""
    if not CLI.exists():
        pytest.skip("C CLI not built")
    work = tmp_path_factory.mktemp("batch")
    src = work / "in"
    src.mkdir()
    shutil.copy(TEST_MZML, src / "a.mzML")
    shutil.copy(TEST_MZML, src / "b.mzML")

    archive = work / "batch.mszx"
    subprocess.run(
        [str(CLI), "--batch", str(src), "-o", str(archive)],
        check=True,
        capture_output=True,
    )
    return archive


@requires_cli
def test_read_dispatches_to_batch_container(batch_archive):
    archive = mscompress.read(str(batch_archive))
    try:
        assert isinstance(archive, MSZXBatchFile)
        assert len(archive) == 2
        assert archive.names == ["a.msz", "b.msz"]
    finally:
        archive.close()


@requires_cli
def test_manifest_is_v2_batch(batch_archive):
    with MSZXBatchFile.open(batch_archive) as archive:
        assert isinstance(archive.manifest, MSZXBatchManifest)
        assert archive.manifest.container == "batch"
        assert archive.manifest.version.startswith("2.")


@requires_cli
def test_spectrum_counts_come_from_the_manifest(batch_archive):
    """num_spectra must be readable without opening a single member."""
    with MSZXBatchFile.open(batch_archive) as archive:
        assert [e.num_spectra for e in archive.entries] == [50, 50]
        assert archive._open_members == {}


@requires_cli
def test_members_open_lazily_and_are_cached(batch_archive):
    with MSZXBatchFile.open(batch_archive) as archive:
        assert archive._open_members == {}
        first = archive[0]
        assert len(first.spectra) == 50
        assert list(archive._open_members) == ["a.msz"]
        # Same object on re-access — not a second mmap.
        assert archive[0] is first
        assert archive["a.msz"] is first


@requires_cli
def test_index_and_name_access_agree(batch_archive):
    with MSZXBatchFile.open(batch_archive) as archive:
        assert archive[1] is archive["b.msz"]
        assert "b.msz" in archive
        assert "nope.msz" not in archive


@requires_cli
def test_iteration_yields_every_member(batch_archive):
    with MSZXBatchFile.open(batch_archive) as archive:
        assert [len(m.spectra) for m in archive] == [50, 50]


@requires_cli
def test_missing_entry_raises(batch_archive):
    with MSZXBatchFile.open(batch_archive) as archive:
        with pytest.raises(KeyError):
            archive["absent.msz"]
        with pytest.raises(IndexError):
            archive[99]


@requires_cli
def test_decompress_round_trips_every_member(batch_archive, tmp_path):
    original = TEST_MZML.read_bytes()
    with MSZXBatchFile.open(batch_archive) as archive:
        written = archive.decompress(tmp_path / "out")

    assert [p.name for p in written] == ["a.mzML", "b.mzML"]
    for path in written:
        assert path.read_bytes() == original


@requires_cli
def test_use_after_close_raises(batch_archive):
    archive = MSZXBatchFile.open(batch_archive)
    archive.close()
    assert archive.closed
    archive.close()  # idempotent
    with pytest.raises(ValueError, match="closed"):
        archive[0]


@requires_cli
def test_pickles_by_reopening(batch_archive):
    import pickle

    with MSZXBatchFile.open(batch_archive) as archive:
        archive[0]  # force a member open; must not be carried into the pickle
        revived = pickle.loads(pickle.dumps(archive))
    try:
        assert revived.names == ["a.msz", "b.msz"]
        assert revived._open_members == {}
    finally:
        revived.close()


@requires_cli
def test_single_file_v1_archive_is_not_routed_to_the_batch_reader(mszx_file_path):
    """The committed v1 golden fixture must keep opening as MSZXFile."""
    archive = mscompress.read(mszx_file_path)
    try:
        assert not isinstance(archive, MSZXBatchFile)
        assert isinstance(archive, mscompress.MSZXFile)
    finally:
        archive.close()


def test_batch_manifest_rejects_a_v1_archive(mszx_file_path):
    with tarfile.open(mszx_file_path, "r") as tar:
        data = json.loads(tar.extractfile("manifest.json").read().decode())
    with pytest.raises(ValueError, match="not a multi-file"):
        MSZXBatchManifest.from_dict(data)


# --------------------------------------------------------------------------
# Writer
# --------------------------------------------------------------------------


@pytest.fixture
def two_mzml(tmp_path):
    src = tmp_path / "in"
    src.mkdir()
    shutil.copy(TEST_MZML, src / "a.mzML")
    shutil.copy(TEST_MZML, src / "b.mzML")
    return src


def test_compress_batch_writes_a_readable_archive(two_mzml, tmp_path):
    out = mscompress.compress_batch(two_mzml, tmp_path / "py.mszx")
    with MSZXBatchFile.open(out) as archive:
        assert archive.names == ["a.msz", "b.msz"]
        assert [e.num_spectra for e in archive.entries] == [50, 50]
        assert [len(m.spectra) for m in archive] == [50, 50]


@requires_cli
def test_python_archive_is_byte_identical_to_the_cli(two_mzml, tmp_path):
    """The whole point of sharing one C writer across producers."""
    cli_out = tmp_path / "cli.mszx"
    subprocess.run(
        [str(CLI), "--batch", str(two_mzml), "-o", str(cli_out)],
        check=True,
        capture_output=True,
    )
    py_out = mscompress.compress_batch(two_mzml, tmp_path / "py.mszx")
    assert py_out.read_bytes() == cli_out.read_bytes()


def test_round_trip_matches_the_original_mzml(two_mzml, tmp_path):
    out = mscompress.compress_batch(two_mzml, tmp_path / "rt.mszx")
    with MSZXBatchFile.open(out) as archive:
        written = archive.decompress(tmp_path / "out")
    original = TEST_MZML.read_bytes()
    assert all(p.read_bytes() == original for p in written)


def test_writer_records_description_and_extra(two_mzml, tmp_path):
    out = mscompress.compress_batch(
        two_mzml,
        tmp_path / "meta.mszx",
        description="cohort A",
        extra={"study_id": "PXD012345"},
    )
    with MSZXBatchFile.open(out) as archive:
        assert archive.manifest.description == "cohort A"
        assert archive.manifest.extra == {"study_id": "PXD012345"}


def test_writer_attaches_annotations(two_mzml, tmp_path):
    from mscompress import MSZXBatchWriter

    payload = b"scan\tpeptide\n1\tPEPTIDE\n"
    out = tmp_path / "ann.mszx"
    with MSZXBatchWriter(out) as writer:
        idx = writer.add(two_mzml / "a.mzML")
        writer.add_annotation(
            idx, payload, "annotations/a.tsv", format="tsv", num_records=1
        )
        writer.set_join_key(idx, "scan_number")

    with MSZXBatchFile.open(out) as archive:
        entry = archive.entries[0]
        assert entry.join_key == "scan_number"
        assert [a.filename for a in entry.annotations] == ["annotations/a.tsv"]
        assert entry.annotations[0].num_records == 1
        assert archive.get_annotation(0, "annotations/a.tsv") == payload


def test_explicit_entry_names_are_deduplicated(two_mzml, tmp_path):
    from mscompress import MSZXBatchWriter

    out = tmp_path / "names.mszx"
    with MSZXBatchWriter(out) as writer:
        writer.add(two_mzml / "a.mzML", name="same.msz")
        writer.add(two_mzml / "b.mzML", name="same.msz")

    with MSZXBatchFile.open(out) as archive:
        assert archive.names == ["same.msz", "same__2.msz"]


def test_writer_accepts_an_open_mzml_file(two_mzml, tmp_path):
    """An already-open MZMLFile reuses its mapping instead of re-opening."""
    out = tmp_path / "reuse.mszx"
    source = mscompress.read(str(two_mzml / "a.mzML"))
    try:
        from mscompress import MSZXBatchWriter

        with MSZXBatchWriter(out) as writer:
            writer.add(source)
    finally:
        source._cleanup()

    with MSZXBatchFile.open(out) as archive:
        assert archive.names == ["a.msz"]


def test_abort_leaves_no_archive(two_mzml, tmp_path):
    from mscompress import MSZXBatchWriter

    out = tmp_path / "aborted.mszx"
    with pytest.raises(RuntimeError, match="boom"):
        with MSZXBatchWriter(out) as writer:
            writer.add(two_mzml / "a.mzML")
            raise RuntimeError("boom")
    assert not out.exists()


def test_non_mzml_input_is_rejected(tmp_path):
    from mscompress import MSZXBatchWriter

    junk = tmp_path / "junk.txt"
    junk.write_text("not an mzML")
    writer = MSZXBatchWriter(tmp_path / "bad.mszx")
    try:
        with pytest.raises(ValueError, match="not an mzML"):
            writer.add(junk)
    finally:
        writer.abort()


def test_no_matching_inputs_raises(tmp_path):
    empty = tmp_path / "empty"
    empty.mkdir()
    with pytest.raises(ValueError, match="No input mzML files matched"):
        mscompress.compress_batch(empty, tmp_path / "none.mszx")


def test_on_progress_is_called_per_entry(two_mzml, tmp_path):
    seen = []
    mscompress.compress_batch(
        two_mzml,
        tmp_path / "prog.mszx",
        on_progress=lambda i, total, path: seen.append((i, total, path.name)),
    )
    assert seen == [(0, 2, "a.mzML"), (1, 2, "b.mzML")]


def test_use_after_finish_raises(two_mzml, tmp_path):
    from mscompress import MSZXBatchWriter

    writer = MSZXBatchWriter(tmp_path / "done.mszx")
    writer.add(two_mzml / "a.mzML")
    writer.finish()
    with pytest.raises(ValueError, match="finished or aborted"):
        writer.add(two_mzml / "b.mzML")


def test_resolve_inputs_is_deterministic_and_deduplicated(two_mzml):
    from mscompress.mszx.batch import resolve_mzml_inputs

    a = two_mzml / "a.mzML"
    resolved = resolve_mzml_inputs([two_mzml, a, a])
    assert [p.name for p in resolved] == ["a.mzML", "b.mzML"]


# --------------------------------------------------------------------------
# v1 archives through the collection reader
# --------------------------------------------------------------------------


def test_batch_file_opens_a_v1_archive_as_one_member(mszx_file_path):
    """One container type reads every .mszx; v1 is a collection of one."""
    with MSZXBatchFile.open(mszx_file_path) as archive:
        assert len(archive) == 1
        assert archive.manifest.container == "single"
        assert archive.manifest.version.startswith("1.")
        entry = archive.entries[0]
        assert entry.entry == "temp.msz"
        assert entry.num_spectra == 100
        # size is absent from a v1 manifest; recovered from the tar header.
        assert entry.size > 0
        assert len(archive[0].spectra) == 100


def test_v1_annotations_survive_the_adaptation(mszx_file_path):
    with MSZXBatchFile.open(mszx_file_path) as archive:
        entry = archive.entries[0]
        assert [a.filename for a in entry.annotations] == ["HSA.pepXML.zst"]
        assert archive.get_annotation(0, "HSA.pepXML.zst")


def test_collection_has_no_single_spectra_attribute(mszx_file_path):
    with MSZXBatchFile.open(mszx_file_path) as archive:
        with pytest.raises(AttributeError, match=r"archive\[0\]\.spectra"):
            archive.spectra


def test_read_still_prefers_msxzfile_for_v1(mszx_file_path):
    """Opting into the collection view is explicit; read() is unchanged."""
    archive = mscompress.read(mszx_file_path)
    try:
        assert isinstance(archive, mscompress.MSZXFile)
    finally:
        archive.close()
