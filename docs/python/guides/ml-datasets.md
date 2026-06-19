# ML dataset adapters

Adapters that present MSZX archives to ML training pipelines.

## PyTorch

Install the extra:

```bash
pip install 'mscompress[torch]'
```

```python
from mscompress.datasets.torch import MSCompressDatasetMember
from torch.utils.data import DataLoader

dataset = MSCompressDatasetMember(
    "run.mszx",
    load_annotation="percolator",   # join PSMs into each example
)

loader = DataLoader(dataset, batch_size=64, num_workers=4)
for batch in loader:
    mz, intensity, peptide, score = batch
    ...
```

`MSCompressDatasetMember` implements the `torch.utils.data.Dataset`
protocol — random-access indexing into the MSZX without a full
materialization pass.

## JAX / grain

Install the extra:

```bash
pip install 'mscompress[jax]'
```

```python
from mscompress.datasets.jax import MSCompressJaxDataset
import grain

source = MSCompressJaxDataset("run.mszx", load_annotation="percolator")
sampler = grain.IndexSampler(num_records=len(source), shard_options=...)
loader = grain.DataLoader(
    data_source=source,
    sampler=sampler,
    operations=[...],  # batching / padding / shuffling
)
for batch in loader:
    ...
```

`MSCompressJaxDataset` implements the grain random-access protocol and
includes collation helpers for ragged peak arrays.

## Streaming joins

Both adapters lazily decode spectra and annotations — opening an MSZX
archive is O(1), and a given example is fetched only when it's requested
by the loader.

## Block size and random reads

Shuffled training is a random-access workload, and every fetch decompresses
the whole ZSTD block holding that spectrum. Archives written with large blocks
(the 100 MB default) make shuffled `DataLoader` reads slow. If your shards were
compressed for archival, [rechunk](rechunking.md) them to a smaller block size
(e.g. `1–4 MB`) first to cut per-read decompression cost.
