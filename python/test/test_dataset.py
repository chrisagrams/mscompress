import os

import pytest

torch = pytest.importorskip("torch")
from torch.utils.data import DataLoader

from mscompress.datasets.torch import MSCompressDataset, MSCompressDatasetMember
from mscompress.types import AnnotationFormat


def _collate(batch):
    # Module-level so it's picklable: macOS/Windows DataLoader workers use the
    # 'spawn' start method, which pickles collate_fn. A test-local closure
    # raises AttributeError on spawn (passes only under fork).
    return [b[0] for b in batch], [b[1] for b in batch]


def test_mscompress_dataset_single_file(msz_file_path):
    dataset = MSCompressDataset(msz_file_path)
    assert len(dataset.members) == 1
    assert len(dataset) > 0
    sample = dataset[0]
    assert isinstance(sample, tuple) and len(sample) == 2
    mz, intensity = sample
    assert mz.ndim == 1 and intensity.ndim == 1


def test_mscompress_dataset_directory(test_data_dir):
    dataset = MSCompressDataset(test_data_dir)
    assert len(dataset.members) > 0
    assert dataset._total_spectra == len(dataset)
    assert len(dataset) > 0

    # Prefix-sum offsets are monotonic and cover every spectrum exactly once.
    assert dataset._offsets[0] == 0
    assert dataset._offsets[-1] == len(dataset)
    assert dataset._offsets == sorted(dataset._offsets)

    # Global indexing resolves to a valid (mz, intensity) tuple for every index.
    for g in range(len(dataset)):
        sample = dataset[g]
        assert isinstance(sample, tuple)
        assert sample[0].ndim == 1 and sample[1].ndim == 1

    # Negative + out-of-range indexing.
    assert torch.equal(dataset[-1][0], dataset[len(dataset) - 1][0])
    with pytest.raises(IndexError):
        dataset[len(dataset)]


def test_dataset_identical_mzml_msz(test_data_dir):
    """test.mzML and test.msz hold identical data; first spectrum matches."""
    mzml = MSCompressDatasetMember(os.path.join(test_data_dir, "test.mzML"))
    msz = MSCompressDatasetMember(os.path.join(test_data_dir, "test.msz"))
    assert torch.equal(mzml[0][0], msz[0][0])
    assert torch.equal(mzml[0][1], msz[0][1])


def test_parent_holds_no_open_handles(test_data_dir):
    """Construction must not keep file handles open or build a per-spectrum
    index — only the lightweight prefix sum and lazy members."""
    dataset = MSCompressDataset(test_data_dir)
    assert dataset._handles == {}
    assert dataset._proc_cache is None
    assert not hasattr(dataset, "_index_lookup")
    assert len(dataset._offsets) == len(dataset._files) + 1


def test_cache_bytes_creates_shared_worker_cache(test_data_dir):
    from mscompress import BlockCache
    dataset = MSCompressDataset(test_data_dir, cache_bytes=8 * 1024 * 1024)
    _ = dataset[0]  # triggers per-process open in this (main) process
    assert isinstance(dataset._proc_cache, BlockCache)
    assert dataset._proc_cache.budget == 8 * 1024 * 1024
    if len(dataset._files) > 1:
        _ = dataset[len(dataset) - 1]
        assert len(dataset._handles) >= 2  # multiple shards, one shared cache


def test_cache_zero_disables(test_data_dir):
    dataset = MSCompressDataset(test_data_dir, cache_bytes=0)
    _ = dataset[0]
    assert dataset._proc_cache == 0  # bounding disabled


def test_max_open_files_evicts(test_data_dir):
    dataset = MSCompressDataset(test_data_dir, max_open_files=1)
    if len(dataset._files) < 2:
        pytest.skip("need >=2 files to test eviction")
    _ = dataset[0]
    _ = dataset[len(dataset) - 1]
    assert len(dataset._handles) <= 1


def test_dataloader_multiworker(test_data_dir):
    """End-to-end: a multi-worker DataLoader iterates the whole dataset."""
    dataset = MSCompressDataset(test_data_dir, cache_bytes=8 * 1024 * 1024)

    loader = DataLoader(
        dataset, batch_size=4, shuffle=True, num_workers=2, collate_fn=_collate
    )
    seen = 0
    for mzs, intens in loader:
        assert len(mzs) == len(intens)
        seen += len(mzs)
    assert seen == len(dataset)


def test_mscompress_dataset_w_annotations(mszx_file_path):
    dataset = MSCompressDataset(
        mszx_file_path, load_annotations=[AnnotationFormat.PEPXML]
    )
    assert len(dataset) > 0
    for i in range(len(dataset)):
        sample = dataset[i]
        assert isinstance(sample, tuple) and len(sample) == 3
        mz, intensity, psm = sample
        assert mz.ndim == 1 and intensity.ndim == 1
        assert psm is not None
