# Performance notes

Rules of thumb for getting the best throughput out of `mscompress`. The
specifics vary by hardware and input — measure on your own data before
committing to a setting.

## Threads

- Default is the CPU core count (`get_num_threads()`).
- Compression scales near-linearly with cores up to memory bandwidth.
- Decompression scales until disk read bandwidth saturates.
- For workloads that hit a shared filesystem or NFS, lower the thread
  count to ~half the cores to avoid I/O thrash.

## Blocksize

- Default `100 MB` is good for archival batch compression.
- Smaller blocks (`10 MB`) — better random-access latency on MSZ reads,
  worse compression ratio (~1–3% larger files).
- Larger blocks (`500 MB+`) — better ratio, worse memory footprint, and
  workers stall waiting for the largest division.

A spectrum-count blocksize (e.g., `1000` spectra per block) is exposed
through the Python `RuntimeArguments` API for finer control.

Already have a compressed file written with large blocks? Use
[`rechunk`](../python/guides/rechunking.md) to rewrite it at a smaller block
size — in place or to a new file — without re-deriving anything from the source
mzML. Handy for producing a random-access-friendly copy for ML training from an
archival, large-block original.

## ZSTD level

- `3` (default) — fastest reasonable level.
- `9` — recommended for general use. ~2x slower than 3, ~5–10% smaller.
- `19+` — archival; significant slowdown for marginal extra compression.
- `22` — max; only useful for "write once, read forever."

Decompression speed is mostly insensitive to compression level.

## Memory mapping

MSZ reads use `mmap`. On Linux, `madvise(MADV_RANDOM)` is set on the
mapping when random-access reads dominate the workload — this prevents
the kernel from prefetching pages that won't be touched.

For large MSZ files (>16 GB on 32-bit hosts, which is rare but possible),
`mmap` failures fall back to seek + read; throughput drops noticeably.

## Memory footprint

Per worker thread:

- One `ZSTD_CCtx` (or `_DCtx`) — a few MB.
- One block-sized scratch buffer.
- The compressed-block queue (grows during the run, freed at flush).

With 16 threads at the default blocksize, peak memory is on the order of
2–3 GB. Reduce blocksize to fit constrained machines.

## Profile before tuning

The CI build collects throughput numbers per platform; don't trust hand-
waved guidance over what shows up on your hardware. Run with `--verbose`
for per-phase timing.
