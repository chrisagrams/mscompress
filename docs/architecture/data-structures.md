# Data structures

The structs declared in `src/mscompress.h` carry the entire pipeline. Five
of them do most of the work.

## `data_format_t`

The runtime plan. Holds source data information parsed from the input mzML
(`source_mz_fmt`, `source_inten_fmt`, `source_compression`, `source_total_spec`)
and target information for the output MSZ
(`target_xml_format`, `target_mz_format`, `target_inten_format`). Plus the
function pointers that drive the pipeline:

- `target_xml_fun`, `target_mz_fun`, `target_inten_fun` — algorithm
  transforms
- `encode_source_compression_*_fun` / `decode_source_compression_*_fun` —
  base64 + zlib codecs
- `*_compression_fun` / `*_decompression_fun` — ZSTD/LZ4 block (de)compressors

The first three (`xml`/`mz`/`inten` format) plus the scale factors are
**serialized** to the MSZ header. The function pointers are runtime-only,
re-resolved on every open.

## `algo_args`

Passed into every algorithm function as `void*`. Carries:

- `src` / `src_len` — the input buffer the encoder reads from
- `dest` / `dest_len` — the output buffer the encoder writes into
- `mz_scale_factor` / `int_scale_factor` — quantization parameters

Buffers are caller-owned; the algorithm function must not store or free
them.

## `footer_t`

88 bytes serialized at the end of an MSZ file. Records everything needed to
re-open the file:

- `xml_pos`, `mz_binary_pos`, `inten_binary_pos` — start of each stream
- `xml_blk_pos`, `mz_binary_blk_pos`, `inten_binary_blk_pos` — start of
  each stream's block-length metadata
- `divisions_t_pos` — start of the divisions table
- `num_spectra`, `original_filesize`, `n_divisions`
- `magic_tag` (`0x035F51B5`)
- `mz_fmt`, `inten_fmt` — source data type accessions

## `division_t` / `divisions_t`

A `division_t` is one worker's chunk: per-spectrum start/end positions for
XML, m/z, and intensity, plus scan numbers, MS levels, retention times.

A `divisions_t` is the array-of-pointers wrapper plus a count. Two
**ownership modes** matter here, because the same struct serves two paths:

| Path | How divisions are allocated | How to free them |
|------|-----------------------------|------------------|
| Compress | `malloc`'d during scan | `dealloc_divisions()` |
| Decompress / read | Pointers into the mmap'd file | `dealloc_read_divisions()` (frees the wrapper structs, not the mapping) |

Mismatching free routines causes either leaks or double-frees — see
[Memory ownership](memory-ownership.md).

## `block_len_t` / `block_len_queue_t`

Linked-list nodes tracking each compressed block's size pair:

- `original_size` — bytes the block decompresses to
- `compressed_size` — bytes on disk
- `cache` — populated on first decompress, reused on subsequent reads

The cache field is what makes selective extraction O(unique blocks) instead
of O(spectra requested).

## `cmp_block_t` / `cmp_blk_queue_t`

Linked-list nodes for **compressed** blocks during the compression phase.
Each worker thread builds its own queue, then the main thread flushes them
in order.

## `zlib_block_t`

Internal buffer struct used by `encode.c` / `decode.c` for the optional
zlib stage. The header (`ZLIB_TYPE = uint32_t`) prepended by
`zlib_pop_header()` is `malloc`'d and must be freed by the caller — easy to
miss in new code paths.
