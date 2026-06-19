# Python Quick Start

Five minutes from install to round-tripped MSZ.

## Install

```bash
pip install mscompress
# or
uv add mscompress
```

## Open an mzML and iterate spectra

```python
import mscompress

with mscompress.read("sample.mzML") as f:
    print(f"{len(f.spectra)} spectra in {f.filesize:,} bytes")
    for s in f.spectra:
        print(f"scan {s.scan:>6}  MS{s.ms_level}  rt={s.retention_time:.2f}s  n={s.size}")
```

`read()` auto-detects mzML vs MSZ and returns the right file object.
`f.spectra` is lazy — iterating doesn't load every binary array up front.

## Get the data as NumPy arrays

```python
s = f.spectra[42]
mz, intensity = s.mz, s.intensity     # 1-D numpy arrays
peaks = s.peaks                       # (N, 2) — same data, stacked
```

## Compress to MSZ

```python
with mscompress.read("sample.mzML") as f:
    f.arguments.threads = 8
    f.arguments.zstd_compression_level = 9
    f.compress("sample.msz")
```

The output is a single `.msz` file. Decompressing it back to mzML:

```python
with mscompress.read("sample.msz") as f:
    f.decompress("sample.roundtrip.mzML")
```

## Random-access reads on MSZ

The whole point of MSZ — pull spectrum 100,000 without decompressing the
99,999 before it.

```python
with mscompress.read("huge.msz") as f:
    s = f.spectra[100_000]   # one block decompressed, not the whole file
    print(s.mz[:5], s.intensity[:5])
```

## Lossy compression

```python
with mscompress.read("sample.mzML") as f:
    f.arguments.mz_lossy = "delta32"
    f.arguments.int_lossy = "log"
    f.compress("sample.lossy.msz")
```

See the [profile picker](../getting-started/choosing-a-profile.md) for
recommendations and the [algorithm catalog](../algorithms/catalog/index.md)
for what each setting trades away.

## What's next

- [Working with spectra](guides/spectra.md) — transforms, lazy loading,
  iteration patterns
- [Extracting subsets](guides/extracting.md) — by index, scan number, MS
  level
- [MSZX archives](guides/mszx.md) — bundle spectra with search results
