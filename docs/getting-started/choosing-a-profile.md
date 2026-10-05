# Choosing a compression profile

A starting point based on what you're optimizing for. All profiles default to
ZSTD level 3 unless noted; bump it up to 19+ for archival storage when the
extra encoding time doesn't matter.

| Goal | m/z algorithm | intensity algorithm | ZSTD level |
|------|---------------|---------------------|------------|
| Maximum fidelity | `lossless` | `lossless` | 9 |
| Reasonable size, full precision | `lossless` | `lossless` | 19 |
| Good size, scientifically safe | `delta32` | `log` | 9 |
| Aggressive, ML training pipeline | `delta24` | `cast` | 9 |
| Maximum compression, exploratory | `bitpack` / `vbr` | `vbr` | 22 |

For day-to-day use, `delta32 + log` is a good default — it cuts file sizes
substantially while keeping m/z error well under 1 ppm and intensity error
under a fraction of a count.

See the [algorithm catalog](../algorithms/catalog/index.md) for per-algorithm
precision characterization, and
[Lossy vs lossless](concepts/lossy-vs-lossless.md) for the underlying
tradeoffs.
