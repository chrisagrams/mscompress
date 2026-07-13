# Build modes

The Python extension build (`python/setup.py`) supports four modes,
controlled by environment variables.

| Mode | Command | Compile flags | Cython directives |
|------|---------|---------------|--------------------|
| Release | `uv sync --all-extras --reinstall` | `-O3` | none |
| Debug | `MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall` | `-g -O0` | `gdb_debug=True` |
| Linetrace | `MSCOMPRESS_LINETRACE=1 uv sync ...` | `-O3` | `linetrace`, `binding` |
| Debug + linetrace | `MSCOMPRESS_DEBUG=1 MSCOMPRESS_LINETRACE=1 uv sync ...` | `-g -O0` | `gdb_debug=True`, `linetrace`, `binding` |

## When to use each

- **Release** — default. What ships on PyPI.
- **Debug** — before running Valgrind or attaching GDB. Compiled symbols
  let the debugger map back to Cython source.
- **Linetrace** — enables Cython linetrace + binding for line-level
  Python coverage and profiling. Significant runtime overhead.
- **Debug + linetrace** — when you need both — typically for diagnosing
  a memory issue in a specific Cython line.

## Memory-leak workflow

```bash
cd python
MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall
valgrind --tool=memcheck --leak-check=full --log-file=leak-check.txt \
    $(uv run which python) -m pytest
```

The macOS `leaks` tool also works on a debug build — see
[Memory leak workflow](../contributing/memory-leaks.md).
