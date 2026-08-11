"""Version handling for MSZXManifest / MSZXBatchManifest.

v1 (single ``spectra_file``) and v2 (multi-file ``spectra_files``) archives are
both readable. What must still FAIL LOUDLY is a manifest from a genuinely newer
mscompress: silently falling back would mis-read an archive whose layout this
build does not understand.

These assertions were inverted when batch support landed — they previously
pinned that any v2 manifest raised.
"""

import json

import pytest

from mscompress.mszx.metadata import (
    MSZXBatchManifest,
    MSZXManifest,
    is_batch_manifest,
)

V2_BATCH = {
    "version": "2.0",
    "container": "batch",
    "spectra_files": [
        {"entry": "a.msz", "original": "a.mzML", "size": 10, "num_spectra": 3},
        {"entry": "b.msz", "original": "b.mzML", "size": 20, "num_spectra": 4},
    ],
}


def test_v1_manifest_still_parses():
    m = MSZXManifest.from_dict(
        {"version": "1.0", "spectra_file": "sample.msz", "num_spectra": 42}
    )
    assert m.version == "1.0"
    assert m.spectra_file == "sample.msz"
    assert m.num_spectra == 42


def test_max_supported_major_is_a_class_constant_not_a_field():
    """A bare annotated assignment in a dataclass body becomes a field.

    That would let MAX_SUPPORTED_MAJOR be passed to __init__ and then be
    silently dropped by to_dict(). ClassVar keeps it a constant.
    """
    assert "MAX_SUPPORTED_MAJOR" not in MSZXManifest.__dataclass_fields__
    with pytest.raises(TypeError):
        MSZXManifest(MAX_SUPPORTED_MAJOR=5)  # type: ignore[call-arg]


def test_v2_batch_manifest_parses():
    m = MSZXBatchManifest.from_dict(V2_BATCH)
    assert m.container == "batch"
    assert len(m) == 2
    assert [e.entry for e in m.spectra_files] == ["a.msz", "b.msz"]
    assert m.spectra_files[0].num_spectra == 3


def test_v2_round_trips_through_json():
    m = MSZXBatchManifest.from_json(json.dumps(V2_BATCH))
    assert MSZXBatchManifest.from_json(m.to_json()).to_dict() == m.to_dict()


def test_v2_optional_fields_may_be_absent():
    """Archives written before num_spectra/join_key/annotations existed."""
    m = MSZXBatchManifest.from_dict(
        {
            "version": "2.0",
            "container": "batch",
            "spectra_files": [{"entry": "a.msz", "original": "a.mzML", "size": 1}],
        }
    )
    assert m.spectra_files[0].num_spectra is None
    assert m.spectra_files[0].annotations == []


def test_batch_detected_even_if_mislabeled_v1():
    data = {"version": "1.0", "spectra_files": [{"entry": "a.msz"}]}
    assert is_batch_manifest(data)
    # Routed away from the single-file reader rather than mis-read as one.
    with pytest.raises(ValueError, match="multi-file"):
        MSZXManifest.from_dict(data)


def test_single_file_manifest_rejected_by_batch_parser():
    with pytest.raises(ValueError, match="not a multi-file"):
        MSZXBatchManifest.from_dict({"version": "1.0", "spectra_file": "a.msz"})


@pytest.mark.parametrize("cls", [MSZXManifest, MSZXBatchManifest])
def test_newer_major_still_rejected(cls):
    with pytest.raises(ValueError, match="newer than this build"):
        cls.from_dict({"version": "3.0", "container": "batch", "spectra_files": []})
