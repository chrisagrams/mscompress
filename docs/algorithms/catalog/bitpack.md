# bitpack

A variable-width bit-packing transform on quantized m/z values. Reads the
needed bit width from the data instead of using a fixed 16/24/32 split.

- **Target stream:** m/z
- **Loss:** Yes — quantization controlled by `--mz-scale-factor`
  (default `10000`)
- **Source:** `src/algos/bitpack.c`

## When to use it

When you've characterized the precision of your data well enough to be
confident in a custom scale factor and want better ratios than fixed-width
delta encoding. Less predictable in compression ratio than the `delta*`
family but can outperform them on certain instruments.

## CLI

```bash
mscompress --mz-lossy bitpack --mz-scale-factor 50000 input.mzML
```
