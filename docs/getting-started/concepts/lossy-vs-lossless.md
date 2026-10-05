# Lossy vs lossless

`mscompress` always wraps the final output in ZSTD (which is lossless), but
the **pre-compression transform** you apply to the m/z and intensity arrays
can be lossy:

| Path | What it does | When to use |
|------|--------------|-------------|
| Lossless | ZSTD only. Original `double` arrays preserved bit-for-bit. | Quantitative re-analysis, regulatory contexts, "I'm not sure yet." |
| Cast | Cast 64-bit doubles to 32-bit (or 16-bit) floats before ZSTD. | When 32-bit precision is plenty for downstream analysis. |
| Delta | Replace each value with the delta from the previous one, then quantize to 16/24/32 bits. | m/z arrays — they're nearly monotonic and small deltas pack well. |
| Log | Log-2 transform before ZSTD. | Intensity arrays with multi-decade dynamic range. |
| Bitpack / VBR | Variable-bit packing of quantized values. | When you need maximum compression and have characterized the precision loss. |

You configure the algorithm per stream — m/z and intensity transforms are
chosen independently. See [Choosing a profile](../choosing-a-profile.md) for
recommendations and the [algorithm catalog](../../algorithms/catalog/index.md)
for per-algorithm details on precision, scale factors, and tradeoffs.

!!! warning
    Lossy paths are **not reversible**. If you compress with `delta16`, the
    decompressed m/z values will not exactly match the original — they'll be
    quantized to ~127 ppm (depending on the scale factor). Always keep the
    original mzML if you might need bit-exact data later.
