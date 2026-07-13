# vbr

Variable bit-rate encoding. Allocates bits per value based on the dynamic
range of each block.

- **Target stream:** m/z and intensity
- **Loss:** Yes — `--mz-scale-factor` (default `0.1`) and
  `--int-scale-factor` (default `1.0`) act as quantization thresholds
- **Source:** `src/algos/vbr.c`

## When to use it

When maximum compression matters more than predictable per-value precision.
Performs especially well on intensity arrays with bursty dynamic range.
Less suitable than `delta*` when you need a stable error bound for
downstream analysis.

## CLI

```bash
mscompress --mz-lossy vbr --int-lossy vbr input.mzML
```
