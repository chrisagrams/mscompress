# Extraction

Extraction is "decompress the subset of spectra you actually want" without
materializing the whole file. Lives in `src/extract.c`.

## Selector types

The CLI exposes three orthogonal selectors; under the hood, they all
resolve to a list of spectrum indices:

| Selector | CLI flag | Python / TS argument |
|----------|----------|---------------------|
| Indices | `--extract-indices 0-100` | `indices=[...]` |
| Scan numbers | `--extract-scans 1-3,5-6` | `scan_numbers=[...]` |
| MS level | `--ms-level 2` | `ms_level=2` |

## Two code paths

### From MSZ (random access)

The footer's divisions table maps each spectrum to its byte range in each
of the three streams. Extraction:

1. Resolves the selector to a list of spectrum indices.
2. Looks up each spectrum's three block addresses.
3. Decompresses only those blocks (with `block_len_t` caching to avoid
   redundant work when blocks contain multiple requested spectra).
4. Emits an mzML wrapper around the extracted spectra.

This is O(selected spectra) regardless of file size — the killer feature of
the MSZ format.

### From mzML (streaming)

When extracting directly from mzML (no compressed form yet), the file is
scanned linearly with `yxml`, spectra are filtered against the selector, and
matching spectra are emitted. Cost is proportional to file size, not
selection size.

## Entry points

| Function | What it does |
|----------|--------------|
| `extract_msz()` | Random-access extraction from a compressed file |
| `extract_mzml_filtered()` | Streaming filter from an mzML file |
