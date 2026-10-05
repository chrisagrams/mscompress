# Troubleshooting

## "Corrupt base64" or "Invalid base64 character" on extraction

The input mzML has malformed base64-encoded binary arrays. `mscompress`
detects this when decoding for re-compression. Common causes:

- Hand-edited mzML files where whitespace got injected into binary
  elements.
- mzML files from older converters that omit the base64 padding.

There's no recovery — the corrupted spectrum can't be encoded. Filter it
out by index or scan number before compressing.

## Empty `<binary></binary>` arrays

Some mzML files contain spectra with empty binary arrays (no peaks). As
of `mscompress 1.0.15`, these are handled gracefully on the extraction
path — the spectrum is preserved with zero-length m/z and intensity
arrays.

## Memory leak under Valgrind

Run with a debug build (`MSCOMPRESS_DEBUG=1`) and the project's suppressions
file. See [Memory leak workflow](../contributing/memory-leaks.md).

## "ImportError: no module named mscompress._core"

The Cython extension didn't build. Reinstall:

```bash
cd python && uv sync --all-extras --reinstall
```

If that fails, you're missing a C compiler. See
[Installation](../getting-started/installation.md).

## Mismatched algorithm on decompress

If the MSZ was written with one algorithm and `mscompress` decompresses it
with another (which shouldn't happen — the algorithm is stored in the
header), the output is garbage. Verify with:

```bash
mscompress --describe file.msz
```

Look at the `mz_format` and `inten_format` lines.

## Large files exhaust memory on compress

Default blocksize is 100 MB. Reduce it for memory-constrained machines:

```python
f.arguments.blocksize = 10_000_000   # 10 MB
```

Or use `compress_stream()` (see the [compressing guide](guides/compressing.md)).
