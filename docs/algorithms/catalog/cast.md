# cast / cast16

Cast 64-bit doubles to 32-bit floats (`cast`) or 16-bit half-floats
(`cast16`) before ZSTD compression. The cheapest possible lossy transform —
no quantization math, just throw away the low-order bits of each value.

| Variant | Bits per value | Default scale | Source |
|---------|----------------|---------------|--------|
| `cast` | 32 | 0 | `src/algos/cast.c` |
| `cast16` | 16 | 11.801 | `src/algos/cast.c` |

- **Target stream:** m/z
- **Loss:** Yes — IEEE-754 truncation

## When to use it

- `cast` — when 32-bit precision is sufficient for downstream analysis and
  you don't need to think about quantization parameters. Halves the m/z
  array size before ZSTD even runs.
- `cast16` — only for visualization or coarse clustering. The scale factor
  (default 11.801) controls the exponent shift; the resulting precision is
  not adequate for peptide identification.

## CLI

```bash
mscompress --mz-lossy cast input.mzML
```
