# mscompress

**Multi-threaded lossless and lossy compression for Mass Spectrometry data.**

`mscompress` defines the `.msz` file format — a random-access compressed
container for mzML data — and ships the C library, CLI, Python bindings, and
Node.js/TypeScript bindings that read and write it. An extended container
format, `.mszx`, bundles a compressed spectra file with search-result
annotations and a JSON manifest.

## What it does

- Reads **mzML** (uncompressed XML) and produces **MSZ** (compressed, random
  access without full decompression).
- Stores XML metadata, m/z arrays, and intensity arrays as three independently
  compressed streams so any spectrum can be retrieved in O(1) seeks.
- Supports a registry of **pre-compression transforms** (delta encoding, log,
  bit-packing, type casts, variable bit-rate) that improve compression ratio
  on numeric arrays.
- Bundles **MSZX** archives that ship search results (pepXML / Percolator TSV)
  alongside the spectra.

## Pick your interface

<div class="grid cards" markdown>

-   **Python**

    ---

    Cython-backed bindings with optional Parquet, PyTorch, and JAX adapters.

    [:octicons-arrow-right-24: Get started](python/index.md)

-   **Node.js / TypeScript**

    ---

    N-API addon with prebuilt binaries for macOS, Linux, and Windows.

    [:octicons-arrow-right-24: Get started](node/index.md)

-   **CLI**

    ---

    Standalone `mscompress` binary for batch compression and extraction.

    [:octicons-arrow-right-24: Get started](cli/index.md)

-   **C library**

    ---

    Core library and architecture — how the format and algorithms work.

    [:octicons-arrow-right-24: Architecture](architecture/index.md)

</div>

## Quick links

- [MSZ format spec](format/msz.md) — byte-level layout
- [Algorithm catalog](algorithms/catalog/index.md) — registered transforms
- [Implementing a new algorithm](algorithms/implementing.md)
- [Contributing](contributing/index.md)
