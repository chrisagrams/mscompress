# delta (16 / 24 / 32)

Replace each m/z value with the **delta** from the previous value, multiply
by the scale factor to push it into integer range, and pack the result into
16, 24, or 32 bits. m/z arrays are nearly monotonically increasing within a
spectrum, so the deltas are small and pack tightly.

| Variant | Bits per delta | Default scale | Source |
|---------|----------------|---------------|--------|
| `delta16` | 16 | 127.998 | `src/algos/delta.c` |
| `delta24` | 24 | 65536 | `src/algos/delta.c` |
| `delta32` | 32 | 262144 | `src/algos/delta.c` |

- **Target stream:** m/z
- **Loss:** Yes — quantization controlled by `--mz-scale-factor`

## How it works

For an input array `m[0], m[1], ..., m[n-1]`:

```
d[0] = m[0]                                            (stored directly)
d[i] = round((m[i] - m[i-1]) * scale)  for i >= 1
```

The decoder reverses:

```
m[0] = d[0]
m[i] = m[i-1] + d[i] / scale
```

Precision is `1 / scale` per delta, but **error accumulates** across a
spectrum because each value depends on the previous decoded value. With the
default `delta32` scale (262144), per-delta precision is ~4 µDa; for a
spectrum of 10,000 peaks, worst-case accumulated error stays well below 1
ppm at typical m/z ranges.

## When to use it

- `delta32` — recommended default for m/z compression. Quantization is
  fine enough for peptide identification at typical instrument resolution.
- `delta24` — for ML training pipelines where 24-bit precision is still
  comfortably below instrument noise floor.
- `delta16` — for visualization-grade compression. Don't use for
  quantitative analysis.

## CLI

```bash
mscompress --mz-lossy delta32 input.mzML
mscompress --mz-lossy delta32 --mz-scale-factor 524288 input.mzML
```
