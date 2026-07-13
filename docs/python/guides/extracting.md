# Extracting subsets

When you only need some of the spectra, push the filter into the extractor
so the C core skips the rest. Faster than reading them all and discarding.

## By index

```python
with mscompress.read("data.msz") as f:
    f.extract("subset.mzML", indices=[0, 1, 100, 5000])
```

Ranges work too:

```python
f.extract("subset.mzML", indices=range(1000, 2000))
```

## By scan number

```python
f.extract("subset.mzML", scan_numbers=[42, 100, 1337])
```

## By MS level

```python
f.extract("ms2.mzML", ms_level=2)
```

## Combining filters

Filters are AND-combined — only spectra matching all active selectors are
emitted.

```python
f.extract("ms2_subset.mzML", ms_level=2, indices=range(0, 10000))
```

## Streaming extraction

For very large output sets, write incrementally:

```python
for chunk in f.extract_stream(ms_level=2, indices=range(0, 100_000)):
    sink.write(chunk)
```

Each yielded chunk is a `bytes` of valid mzML fragments; the streaming
form never materializes the full output in memory.

## Why this is fast on MSZ

MSZ's footer records exactly which block each spectrum lives in. The
extractor:

1. Resolves the selector to a list of indices.
2. Looks up each index's block address.
3. Decompresses only the blocks that contain at least one selected
   spectrum (with caching so a block decompresses once even if multiple
   selected spectra share it).

Cost is O(selected spectra), not O(file size). Extracting 100 MS2 spectra
from a 50 GB MSZ takes milliseconds.
