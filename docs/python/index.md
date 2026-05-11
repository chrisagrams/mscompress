# Python

Cython-backed bindings to the `mscompress` C core. Multi-threaded
compression, random-access reads on `.msz`, and adapters for Parquet,
PyTorch, and JAX.

- **[Quick Start](quickstart.md)** — open mzML, iterate spectra, compress
  to MSZ, read it back.
- **[Guides](guides/index.md)** — task-oriented recipes for each major
  feature.
- **[API Reference](reference/index.md)** — generated from docstrings.
- **[Build modes](build-modes.md)** — debug, linetrace, Valgrind workflow.
- **[Troubleshooting](troubleshooting.md)** — common errors and how to
  diagnose them.

## At a glance

```python
import mscompress

# Auto-detect mzML vs MSZ
with mscompress.read("data.mzML") as f:
    print(f"{len(f.spectra)} spectra")
    for s in f.spectra:
        print(s.scan, s.ms_level, s.mz.shape)

    f.arguments.threads = 8
    f.arguments.zstd_compression_level = 9
    f.compress("data.msz")

with mscompress.read("data.msz") as f:
    spec = f.spectra[100]    # O(1) seek + one block decompress
    peaks = spec.peaks       # numpy (N, 2) array
```
