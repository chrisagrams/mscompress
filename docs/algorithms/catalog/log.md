# log

Apply a log-2 transform to intensity values before ZSTD compression. MS
intensities span many orders of magnitude (10⁰ to 10⁹), and the log domain
is dramatically more compressible.

- **Target stream:** intensity
- **Loss:** Yes — quantization controlled by `--int-scale-factor`
  (default `72`)
- **Source:** `src/algos/log2.c`

## How it works

```
encoded[i] = round(log2(intensity[i] + 1) * scale)
decoded[i] = 2 ^ (encoded[i] / scale) - 1
```

The `+ 1` shift handles zero intensities. The default scale of 72 gives
roughly 14-bit precision in the log domain — fine for peak picking, ML
features, or any application where relative intensities matter more than
absolute counts.

## When to use it

The standard companion to a `delta*` m/z transform. Intensity arrays
benefit enormously from log compression; combine `--mz-lossy delta32 --int-lossy log`
for a balanced lossy profile.

## CLI

```bash
mscompress --int-lossy log input.mzML
```
