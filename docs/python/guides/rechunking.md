# Rechunking files

`rechunk` rewrites an existing `.msz` or `.mszx` file at a different ZSTD
**block size** without changing its data — either in place or to a new file.

```python
import mscompress

# To a new file
mscompress.rechunk("run.msz", "4MB", output="run_4mb.msz")

# In place (atomic replace)
mscompress.rechunk("run.msz", "4MB")
```

## Why rechunk?

Each spectrum lives inside one ZSTD block, and reading a single spectrum
decompresses the **entire** block that contains it. With the default
`blocksize` of 100 MB, a random read decompresses ~100 MB to return a few
kilobytes — fine for sequential scans, painful for random access (e.g. a
shuffled ML `DataLoader`).

Smaller blocks cut that read amplification at a modest cost in compression
ratio:

| Block size | Random reads | Compression ratio | Memory per read |
|------------|--------------|-------------------|-----------------|
| Large (100 MB) | slow | best | high |
| Small (1–4 MB) | fast | ~1–3% larger | low |

Rechunk lets you compress once for archival (large blocks, best ratio) and
later produce a random-access-friendly copy (small blocks) for training,
without re-deriving anything from the source mzML yourself.

### Measured trade-off

The curve below sweeps the block size of an ~12 GB MSZX dataset and measures
shuffled random reads through a single PyTorch `DataLoader`. Throughput (left
axis) peaks sharply, while on-disk size (right axis) keeps shrinking with larger
blocks but flattens quickly — so the best operating point is the smallest block
that still compresses well.

--8<-- "docs/_charts/blocksize_sweep.html"

For this dataset the knee is around **1 MB**: ~35× the random-read throughput of
the 64 MB default, for only ~6% more on disk. Past ~4 MB the compression gain is
marginal while throughput collapses. Your numbers will shift with cache size,
worker count, and data, but the shape of the curve — and the location of the
knee — is the thing to look for. To find it on your own data, rechunk a sample
of shards at several block sizes and time random reads through a `DataLoader`.

See [Performance notes](../../reference/performance.md#blocksize) for the
broader trade-off and [ML dataset adapters](ml-datasets.md) for the workload
that motivates it.

## API

Both a standalone function and methods on the file objects are available:

```python
# Standalone — handles .msz and .mszx
mscompress.rechunk(input, blocksize, output=None, *, threads=None)

# Methods
msz.rechunk(blocksize, output=None, *, threads=None)    # MSZFile
mszx.rechunk(blocksize, output=None, *, threads=None)   # MSZXFile
```

| Argument | Meaning |
|----------|---------|
| `blocksize` | Target block size, as bytes (`4_000_000`) or a size string (`"4MB"`, decimal suffixes matching the CLI). |
| `output` | `None` (default) rewrites the file **in place**; a path writes a **new file** and leaves the input untouched. |
| `threads` | Optional worker-thread count for the re-compression. |

The call returns a freshly opened `MSZFile` / `MSZXFile` for the output.

## What is preserved

Rechunk round-trips the file through mzML in a temporary directory and
re-compresses it, **preserving the original compression configuration** — lossy
algorithms, scale factors, and per-stream target formats are recovered from the
file and re-applied. Only the block size changes.

!!! note "Lossy files are safe to rechunk"
    The lossy algorithms are precision-reducing quantizers, so re-applying one
    to already-decoded data with the original scale factor is idempotent: a
    rechunked lossy file carries the same values as its source, with no extra
    precision lost.

The one setting that is *not* stored in the file is the ZSTD compression
*level*; a rechunked file is written at the default level. The decompressed
data is unaffected.

## In place vs new file

```python
# New file — input is left untouched, returns a handle to the output.
out = mscompress.rechunk("run.msz", "4MB", output="run_4mb.msz")

# In place — atomically replaces the original after a successful re-compress.
mscompress.rechunk("run.msz", "4MB")
```

!!! warning "In-place closes the source handle"
    In-place mode releases the file's memory mapping before replacing it on
    disk, so a handle you already hold becomes unusable afterwards. Use the
    returned object (or re-`read()` the path) to keep working:

    ```python
    msz = mscompress.read("run.msz")
    msz = msz.rechunk("4MB")   # old `msz` is closed; reuse the return value
    ```

    The replace is atomic — on any failure the original file is left intact.

## MSZX archives

Rechunking an `.mszx` re-compresses the embedded MSZ at the new block size and
rebuilds the archive with its manifest metadata and annotation files intact:

```python
mscompress.rechunk("run.mszx", "4MB", output="run_4mb.mszx")
```

## Inspecting block layout

`MSZFile.n_divisions` reports how many independently compressed blocks a file
holds — handy for confirming a rechunk took effect (smaller blocks yield more
divisions):

```python
before = mscompress.read("run.msz").n_divisions
after = mscompress.read("run_4mb.msz").n_divisions
assert after > before
```

## Scratch space

Rechunking decompresses the file to mzML in the system temp directory before
re-compressing, so transient disk use can far exceed the compressed size. Point
`TMPDIR` at a large, fast disk when rechunking big files.
