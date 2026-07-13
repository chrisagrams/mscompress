# MSZ format

The on-disk MSZ file. All multi-byte values are little-endian.

## File layout

```
+0          Header (512 bytes)
+512        XML stream                    (ZSTD-compressed mzML body)
            m/z binary stream             (algo → optional zlib → ZSTD blocks)
            Intensity binary stream       (algo → optional zlib → ZSTD blocks)
            XML block-length metadata
            m/z block-length metadata
            Intensity block-length metadata
            Divisions table
-88         Footer (88 bytes)
```

Stream and metadata positions are recorded in the footer, so the body
sections need not appear in a fixed order — the reader follows footer
offsets.

## Header (512 bytes)

| Offset | Size | Field | Source constant |
|--------|------|-------|-----------------|
| 0 | 4 | `magic_tag` = `0x035F51B5` | `MAGIC_TAG` |
| 4 | 4 | `version_major` = 1 | `FORMAT_VERSION_MAJOR` |
| 8 | 4 | `version_minor` = 0 | `FORMAT_VERSION_MINOR` |
| 12 | 128 | `message` — ASCII `"MS Compress Format 1.0 Gao Laboratory at UIC"`, NUL-padded | `MESSAGE_OFFSET`, `MESSAGE_SIZE` |
| 140 | 36 | Serialized `data_format_t` (public fields) | `DATA_FORMAT_T_OFFSET`, `DATA_FORMAT_T_SIZE` |
| 176 | 8 | `blocksize` (long) | `BLOCKSIZE_OFFSET` |
| 184 | 32 | MD5 checksum (hex string, optional) | `MD5_OFFSET`, `MD5_SIZE` |
| 216 | 296 | Reserved / zero padding | — |
| **Total** | **512** | | `HEADER_SIZE` |

The serialized `data_format_t` (bytes 140–175) holds the **persistent**
fields only: source mz/intensity formats, source compression, source total
spectra, target xml/mz/intensity formats, mz/int scale factors. Function
pointers and runtime fields are not on disk; they're re-resolved on every
open.

## Footer (88 bytes)

Written at the **end** of the file. The reader seeks `filesize - 88` and
parses:

| Field | Type | Purpose |
|-------|------|---------|
| `xml_pos` | `uint64` | Byte offset of XML stream start |
| `mz_binary_pos` | `uint64` | Byte offset of m/z stream start |
| `inten_binary_pos` | `uint64` | Byte offset of intensity stream start |
| `xml_blk_pos` | `uint64` | Byte offset of XML block-length metadata |
| `mz_binary_blk_pos` | `uint64` | Byte offset of m/z block-length metadata |
| `inten_binary_blk_pos` | `uint64` | Byte offset of intensity block-length metadata |
| `divisions_t_pos` | `uint64` | Byte offset of divisions table |
| `num_spectra` | `size_t` | Total spectra in this file |
| `original_filesize` | `uint64` | Pre-compression mzML file size |
| `n_divisions` | `int` | Number of divisions (worker partitions) |
| `magic_tag` | `int` | `0x035F51B5` — sanity check |
| `mz_fmt` | `int` | Source m/z data type accession (e.g., `_64d_`) |
| `inten_fmt` | `int` | Source intensity data type accession |

Defined in `src/mscompress.h` as `footer_t`. Serialized by `write_footer()`
in `src/file.c`; parsed by `read_footer()`.

## Streams

Each of XML, m/z, and intensity is a sequence of independently compressed
**blocks**. Block sizes (both compressed and decompressed) are recorded in
the per-stream block-length metadata so a reader can index into the stream
without scanning it.

The m/z and intensity streams optionally pass through:

- An algorithm transform (`target_mz_fun` / `target_inten_fun`)
- An inner zlib + base64 encoding (legacy mzML compatibility)
- ZSTD or LZ4 compression

The XML stream skips the algorithm and inner encoding layers and only ZSTD-
compresses.

## Block-length metadata

A `block_len_queue_t` (linked-list-on-disk) of `(original_size, compressed_size)`
pairs, one entry per block. The reader walks this list to find the byte
range of any block, plus uses `original_size` to allocate the right output
buffer.

## Divisions table

Records per-division metadata — spectrum count, byte ranges within each
stream, scan numbers, MS levels, retention times. This is the index that
makes selective extraction O(spectra requested) instead of O(file size).

## Data type accessions

The `mz_fmt` and `inten_fmt` fields in the footer use mzML
[PSI-MS](https://www.ebi.ac.uk/ols/ontologies/ms) accession numbers:

| Constant | Value | Meaning |
|----------|-------|---------|
| `_32i_` | 1000519 | 32-bit signed integer |
| `_16e_` | 1000520 | 16-bit float (half) |
| `_32f_` | 1000521 | 32-bit float |
| `_64i_` | 1000522 | 64-bit integer |
| `_64d_` | 1000523 | 64-bit double |

## Algorithm IDs

| Constant | Value | Algorithm |
|----------|-------|-----------|
| `_lossless_` | 4700000 | Pass-through |
| `_ZSTD_compression_` | 4700001 | (compression-method marker) |
| `_cast_64_to_32_` | 4700002 | `cast` |
| `_log2_transform_` | 4700003 | `log` |
| `_delta16_transform_` | 4700004 | `delta16` |
| `_delta24_transform_` | 4700005 | `delta24` |
| `_delta32_transform_` | 4700006 | `delta32` |
| `_vbr_` | 4700007 | `vbr` |
| `_bitpack_` | 4700008 | `bitpack` |
| `_vdelta16_transform_` | 4700009 | `vdelta16` (experimental) |
| `_vdelta24_transform_` | 4700010 | `vdelta24` (experimental) |
| `_cast_64_to_16_` | 4700011 | `cast16` |

See [Algorithm catalog](../algorithms/catalog/index.md) for what each one
does.
