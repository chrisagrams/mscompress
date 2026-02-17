# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

mscompress is a multi-threaded lossless/lossy compression library for Mass Spectrometry (MS) data. It defines a compressed file format (`.msz`) that supports random-access reads without full decompression. The project has a C core library, a CLI tool, and Python bindings via Cython.

## Build & Test Commands

### C library + CLI
```bash
# Configure (from repo root, only needed once or after CMakeLists changes)
cmake -S . -B .

# Build
cmake --build .

# Run all C tests
ctest --test-dir cli

# Run a single C test by name
ctest --test-dir cli -R CompressTest
ctest --test-dir cli -R "MZLossy_delta32"
```

### Python bindings
```bash
# Install/reinstall with all extras (from python/ dir)
cd python && uv sync --all-extras --reinstall

# Run all Python tests (84 tests)
cd python && uv run pytest

# Run a single test file or test
cd python && uv run pytest test/test_mzml.py
cd python && uv run pytest test/test_mzml.py::test_mzml_open -v

# Memory leak checking with Valgrind (build with debug symbols first)
cd python && MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall
cd python && valgrind --tool=memcheck --leak-check=full --log-file=leak-check.txt $(uv run which python) -m pytest
```

Test data lives in `python/test/data/` and is shared by both C and Python tests.

### Python build modes

Build behavior is controlled via environment variables in `setup.py`:

| Mode | Command | Compile flags | Cython directives |
|------|---------|---------------|--------------------|
| Release | `uv sync --all-extras --reinstall` | `-O3` | none |
| Debug | `MSCOMPRESS_DEBUG=1 uv sync ...` | `-g -O0` | `gdb_debug=True` |
| Linetrace | `MSCOMPRESS_LINETRACE=1 uv sync ...` | `-O3` | `linetrace`, `binding` |
| Debug + Linetrace | `MSCOMPRESS_DEBUG=1 MSCOMPRESS_LINETRACE=1 uv sync ...` | `-g -O0` | `gdb_debug=True`, `linetrace`, `binding` |

- **`MSCOMPRESS_DEBUG=1`** — Adds `-g` debug symbols and disables optimization (`-O0`). Use before Valgrind or GDB.
- **`MSCOMPRESS_LINETRACE=1`** — Enables Cython linetrace + binding for Python-level profiling/coverage. Adds runtime overhead.

## Architecture

### Three-stream MSZ format
The `.msz` file stores MS data as three independently compressed streams:
- **XML** — spectrum metadata (default: ZSTD)
- **m/z** — mass-to-charge binary arrays (default: ZSTD, with optional lossy pre-processing)
- **Intensity** — signal intensity binary arrays (default: ZSTD, with optional lossy pre-processing)

A 512-byte header and a variable-length footer enable random access by storing block lengths and division offsets.

### C core (`src/`)
| File | Role |
|------|------|
| `mscompress.h` | All type definitions and function declarations |
| `preprocess.c` | XML parsing (yxml), file division, spectrum scanning, format detection |
| `compress.c` | Multi-threaded compression pipeline |
| `decompress.c` | Multi-threaded decompression |
| `extract.c` | Spectrum extraction from mzML/MSZ with index-based filtering |
| `algo.c` | Data transformation algorithms (lossy encoders/decoders for m/z and intensity) |
| `encode.c` / `decode.c` | Base64 and zlib encode/decode wrappers |
| `queue.c` | `block_len_t` and compressed block queue management |
| `file.c` | File I/O, memory mapping, header/footer serialization |
| `mem.c` | Allocation helpers for data blocks |

### Key data structures
- **`division_t`** — A chunk of the file with position mappings for XML, m/z, and intensity data. Has two ownership modes: malloc'd (compress path) vs mmap-backed (decompress/read path), requiring different dealloc functions.
- **`data_format_t`** — Compression format metadata with function pointers for compress/decompress operations.
- **`block_len_t`** — Linked list node tracking compressed/decompressed block sizes. Supports caching decompressed data to avoid redundant work.
- **`footer_t`** — File footer with stream offsets, block length metadata, and division info for random access.

### Python bindings (`python/`)
- `mscompress/_core.pyx` — Main Cython module exposing C library to Python
- `mscompress/_headers.pxi` — C function/struct declarations for Cython
- Key classes: `BaseFile`, `MZMLFile`, `MSZFile`, `Spectrum`, `Spectra`, `Division`, `DataPositions`
- `mscompress/mszx.py` — Extended format (MSZX) support in pure Python
- `mscompress/metadata/` — Dataset metadata builders (CROISSANT format)
- `mscompress/annotations/` — PSM/pepXML/TSV annotation readers

The Python build (`setup.py`) compiles all C sources + vendor libraries into a single Cython extension with architecture-specific SIMD flags for the base64 library.

### Vendor libraries (`vendor/`)
- **zlib** (Cloudflare fork), **zstd**, **lz4** — compression backends
- **base64** — SIMD-optimized base64 codec (AVX2, SSSE3, SSE4, NEON)
- **yxml** — lightweight streaming XML parser

## Memory Management Rules

These are critical for avoiding leaks and use-after-free:

- **Malloc'd divisions** (compress path): free with `dealloc_division()` / `dealloc_divisions()`
- **Mmap-backed divisions** (decompress/read path): free with `dealloc_read_division()` / `dealloc_read_divisions()` (only frees struct wrappers, not the mapped memory)
- **`encode_base64()`** frees its `zlib_block_t*` parameter internally — never free it yourself
- **`zlib_pop_header()`** returns malloc'd memory — save the pointer and free it after use
- **Encode functions** advance `char**` src pointers during loops — save the original pointer before the loop for cleanup
- **`block_len_t` caching**: check `if (!blk->cache)` before decompressing to avoid redundant allocations

## Versioning

The project version is defined in `.cz.toml` (commitizen) — this is the single source of truth. All other files read from it:

- **CMake** — each `CMakeLists.txt` parses `.cz.toml` with a regex to set `PROJECT_VERSION`
- **C code** — `cli/CMakeLists.txt` passes `VERSION` via `target_compile_definitions`
- **Python** — `python/pyproject.toml` has a static `version` field, updated by `cz bump`
- **npm** — all `package.json` files are updated by `cz bump`

To bump: `cz bump --increment <PATCH|MINOR|MAJOR>`

Never hardcode a version string in `src/mscompress.h` or any CMakeLists.txt — it comes from `.cz.toml`.

## CI/CD

GitHub Actions (`.github/workflows/build.yml`) runs:
1. **build-cli** — CMake build + ctest on Linux/Windows/macOS (x86_64, arm64)
2. **build-python** — cibuildwheel for CPython 3.9–3.13 + pytest
3. **publish-python** — PyPI publish on version tags
4. **build-docker** — Multi-arch Docker image on version tags
