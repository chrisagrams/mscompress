import numpy as np
import pytest

from mscompress.types import AnnotationFormat
from mscompress.datasets.jax import (
    MSCompressJaxDataset,
    list_collate,
    pad_collate,
)


def test_jax_dataset_single_file(msz_file_path):
    dataset = MSCompressJaxDataset(msz_file_path)
    assert len(dataset.members) == 1
    member = next(iter(dataset.members.values()))
    assert len(member) > 0
    sample = member[0]
    assert isinstance(sample, tuple)
    assert len(sample) == 2
    mz, intensity = sample
    assert isinstance(mz, np.ndarray)
    assert isinstance(intensity, np.ndarray)
    assert mz.ndim == 1
    assert intensity.ndim == 1
    assert mz.shape == intensity.shape


def test_jax_dataset_directory(test_data_dir):
    dataset = MSCompressJaxDataset(test_data_dir)
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
    assert np.array_equal(sample_mzml[0], sample_msz[0])
    assert np.array_equal(sample_mzml[1], sample_msz[1])


def test_jax_dataset_w_annotations(mszx_file_path):
    dataset = MSCompressJaxDataset(
        mszx_file_path,
        load_annotations=[AnnotationFormat.PEPXML],
    )
    assert len(dataset.members) > 0
    total_spectra = sum(len(member) for member in dataset.members.values())
    assert total_spectra > 0
    assert dataset._total_spectra == total_spectra

    for member in dataset.members.values():
        for i in range(len(member)):
            sample = member[i]
            assert isinstance(sample, tuple)
            assert len(sample) == 3
            mz, intensity, psm = sample
            assert isinstance(mz, np.ndarray)
            assert isinstance(intensity, np.ndarray)
            assert mz.ndim == 1
            assert intensity.ndim == 1
            assert psm is not None


def test_jax_dataset_global_getitem(test_data_dir):
    dataset = MSCompressJaxDataset(test_data_dir)
    sample0 = dataset[0]
    assert isinstance(sample0, tuple)
    assert len(sample0) in (2, 3)
    mz, intensity = sample0[0], sample0[1]
    assert isinstance(mz, np.ndarray)
    assert isinstance(intensity, np.ndarray)

    # Negative indexing
    sample_last = dataset[-1]
    sample_last_pos = dataset[len(dataset) - 1]
    assert np.array_equal(sample_last[0], sample_last_pos[0])
    assert np.array_equal(sample_last[1], sample_last_pos[1])

    with pytest.raises(IndexError):
        dataset[len(dataset)]


def test_list_collate_passthrough(msz_file_path):
    dataset = MSCompressJaxDataset(msz_file_path)
    member = next(iter(dataset.members.values()))
    n = min(4, len(member))
    batch = [member[i] for i in range(n)]

    mzs, intensities = list_collate(batch)
    assert isinstance(mzs, list)
    assert isinstance(intensities, list)
    assert len(mzs) == n
    assert len(intensities) == n
    for original, collated_mz, collated_int in zip(batch, mzs, intensities):
        assert collated_mz is original[0]
        assert collated_int is original[1]


def test_pad_collate_shapes(msz_file_path):
    dataset = MSCompressJaxDataset(msz_file_path)
    member = next(iter(dataset.members.values()))
    n = min(4, len(member))
    batch = [member[i] for i in range(n)]
    individual_lens = [item[0].shape[0] for item in batch]
    expected_max = max(individual_lens)

    mz, intensity, mask = pad_collate(batch)

    assert mz.shape == (n, expected_max)
    assert intensity.shape == (n, expected_max)
    assert mask.shape == (n, expected_max)
    assert mask.dtype == np.bool_
    assert mz.dtype == batch[0][0].dtype
    assert intensity.dtype == batch[0][1].dtype

    for i, length in enumerate(individual_lens):
        assert mask[i, :length].all()
        if length < expected_max:
            assert not mask[i, length:].any()
            assert (mz[i, length:] == 0).all()
            assert (intensity[i, length:] == 0).all()
        np.testing.assert_array_equal(mz[i, :length], batch[i][0])
        np.testing.assert_array_equal(intensity[i, :length], batch[i][1])


def test_pad_collate_no_mask(msz_file_path):
    dataset = MSCompressJaxDataset(msz_file_path)
    member = next(iter(dataset.members.values()))
    batch = [member[i] for i in range(min(2, len(member)))]
    result = pad_collate(batch, return_mask=False)
    assert len(result) == 2
    mz, intensity = result
    assert mz.ndim == 2
    assert intensity.ndim == 2


def test_pad_collate_with_annotations(mszx_file_path):
    dataset = MSCompressJaxDataset(
        mszx_file_path,
        load_annotations=[AnnotationFormat.PEPXML],
    )
    member = next(iter(dataset.members.values()))
    batch = [member[i] for i in range(min(2, len(member)))]

    mz, intensity, mask, annotations = pad_collate(batch)
    assert mz.ndim == 2 and intensity.ndim == 2 and mask.ndim == 2
    assert isinstance(annotations, list)
    assert len(annotations) == len(batch)
    for ann in annotations:
        assert isinstance(ann, dict)


def test_grain_pipeline_smoke(test_data_dir):
    grain = pytest.importorskip("grain")
    dataset = MSCompressJaxDataset(test_data_dir)
    pipeline = grain.MapDataset.source(dataset).batch(2, batch_fn=list_collate)
    first = next(iter(pipeline))
    assert isinstance(first, tuple)
    mzs, intensities = first[0], first[1]
    assert isinstance(mzs, list)
    assert isinstance(intensities, list)
    assert len(mzs) == 2
    assert all(isinstance(x, np.ndarray) for x in mzs)
