import pytest

torch = pytest.importorskip("torch")

from mscompress.datasets.torch import MSCompressDataset
from mscompress.types import AnnotationFormat

def test_mscompress_dataset_single_file(msz_file_path):
    dataset = MSCompressDataset(msz_file_path)
    assert len(dataset.members) == 1
    member = next(iter(dataset.members.values()))
    assert len(member) > 0
    sample = member[0]
    assert isinstance(sample, tuple)
    assert len(sample) == 2
    mz, intensity = sample
    assert mz.ndim == 1
    assert intensity.ndim == 1


def test_mscompress_dataset_directory(test_data_dir):
    dataset = MSCompressDataset(test_data_dir)
    assert len(dataset.members) > 0
    total_spectra = sum(len(member) for member in dataset.members.values())
    assert total_spectra > 0
    assert dataset._total_spectra == total_spectra

    # Test global indexing
    global_index = 0
    for member in dataset.members.values():
        for local_index in range(len(member)):
            sample = dataset._index_lookup[global_index]
            assert sample[0] is member
            assert sample[1] == local_index
            global_index += 1
    assert global_index == total_spectra

    # Test same sample from test.mzML and test.msz (identical data)
    mzml_member = dataset.members["test.mzML"]
    msz_member = dataset.members["test.msz"]
    sample_mzml = mzml_member[0]
    sample_msz = msz_member[0]
    assert torch.equal(sample_mzml[0], sample_msz[0])
    assert torch.equal(sample_mzml[1], sample_msz[1])


def test_mscompress_dataset_w_annotations(mszx_file_path):
    dataset = MSCompressDataset(
        mszx_file_path,
        load_annotations=[AnnotationFormat.PEPXML]
    )
    assert len(dataset.members) > 0
    total_spectra = sum(len(member) for member in dataset.members.values())
    assert total_spectra > 0
    assert dataset._total_spectra == total_spectra

    # Check that annotations are accessible
    for member_name, member in dataset.members.items():
        for i in range(len(member)):
            sample = member[i]
            assert isinstance(sample, tuple)
            assert len(sample) == 3
            mz, intensity, psm = sample
            assert mz.ndim == 1
            assert intensity.ndim == 1
            assert psm is not None
