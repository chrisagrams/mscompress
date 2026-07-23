"""Version-reject guard for MSZXManifest.from_dict / from_json.

This v1-only binding must FAIL LOUDLY on a v2/batch manifest instead of
silently mis-reading it (previously it fell back to the default single
``spectra_file`` and then failed to locate that entry). See the C CLI's
batch writer for the v2 ``spectra_files`` schema it now refuses.
"""

import json

import pytest

from mscompress.mszx.metadata import MSZXManifest


def test_v1_manifest_still_parses():
    m = MSZXManifest.from_dict(
        {"version": "1.0", "spectra_file": "sample.msz", "num_spectra": 42}
    )
    assert m.version == "1.0"
    assert m.spectra_file == "sample.msz"
    assert m.num_spectra == 42


def test_v2_batch_manifest_rejected():
    with pytest.raises(ValueError, match="newer than this build"):
        MSZXManifest.from_dict(
            {
                "version": "2.0",
                "container": "batch",
                "spectra_files": [
                    {"entry": "a.msz", "original": "a.mzML", "size": 1}
                ],
            }
        )


def test_spectra_files_rejected_even_if_mislabeled_v1():
    with pytest.raises(ValueError):
        MSZXManifest.from_dict(
            {"version": "1.0", "spectra_files": [{"entry": "a.msz"}]}
        )


def test_from_json_rejects_v2():
    with pytest.raises(ValueError):
        MSZXManifest.from_json(
            json.dumps({"version": "2.0", "container": "batch"})
        )
