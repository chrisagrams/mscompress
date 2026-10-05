# Memory leak workflow

The C core uses careful ownership conventions (see
[Memory ownership](../architecture/memory-ownership.md)). When a leak
shows up, the workflow below catches it.

## Valgrind (Linux)

```bash
cd python
MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall

valgrind --tool=memcheck --leak-check=full \
         --log-file=leak-check.txt \
         $(uv run which python) -m pytest
```

The debug build (`MSCOMPRESS_DEBUG=1`) adds `-g -O0`, which makes
Valgrind's stack traces map cleanly to Cython source lines.

## macOS `leaks`

```bash
cd python
MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall

MallocStackLogging=1 $(uv run which python) -m pytest &
PID=$!
sleep 5
leaks $PID > leaks-report.txt
kill $PID
```

For richer per-call analysis, the repo ships a
`cpython-memory-leak-detector` workflow that wraps `leaks`. See the
project root.

## CLI

The CLI binary itself can be run under Valgrind:

```bash
cmake -S . -B . -DCMAKE_BUILD_TYPE=Debug
cmake --build .
valgrind --tool=memcheck --leak-check=full ./cli/mscompress in.mzML out.msz
```

## Common leak sources

- **Mismatched division dealloc** — using `dealloc_division()` on an
  mmap-backed division (or vice versa). See
  [Memory ownership](../architecture/memory-ownership.md).
- **Double-free on `encode_base64()` input** — the function frees its
  `zlib_block_t` parameter; do not free it after the call.
- **`zlib_pop_header()` return value** — the 4-byte header is malloc'd;
  free it after use.

When you find a new failure mode worth documenting, add it to
[Memory ownership](../architecture/memory-ownership.md).
