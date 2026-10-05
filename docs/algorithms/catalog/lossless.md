# lossless

The identity transform. Source bytes flow through unchanged; ZSTD does all
the work.

- **Target streams:** m/z and intensity
- **Loss:** none
- **Source:** `src/algos/lossless.c`
- **CLI:** the default when neither `--mz-lossy` nor `--int-lossy` is set

Use when you can't accept any precision loss. The compression ratio depends
entirely on ZSTD and the input data — typically 2–3x on raw mzML arrays.
