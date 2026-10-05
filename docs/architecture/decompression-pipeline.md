# Decompression pipeline

`decompress.c` reverses the compression pipeline. Entry point is
`decompress_msz()`.

## Phases

1. **Read footer** (`file.c::read_footer`) — the last 88 bytes of the file
   give stream offsets, the divisions offset, spectrum count, and source
   data formats.
2. **Memory-map** — the whole MSZ is mmap'd. Stream blocks are read directly
   from the mapping; no input copy.
3. **Plan** — `set_decompress_runtime_variables()` resolves
   `(algorithm, accession)` to decoder function pointers and stores them on
   `data_format_t`.
4. **Decompress** — `decompress_parallel()` spawns `N` threads of
   `decompress_routine()`. Each thread:
    - Allocates its own `ZSTD_DCtx`.
    - Iterates blocks in its division.
    - ZSTD decompress → optional base64/zlib decode → algorithm decoder →
      original-typed array → emit mzML XML wrapping the array.
5. **Concatenate** — output is written in original spectrum order.

## Block caching

`block_len_t` carries a `cache` field. During selective extraction, a block
that's already been decompressed isn't decompressed a second time:

```c
if (!blk->cache) {
    blk->cache = zstd_decompress(blk->mem, blk->compressed_size, ...);
}
```

This matters for extraction patterns that touch the same block multiple
times (e.g., scan-number filters that span block boundaries). See
[`extract.c`](extraction.md).

## Entry points

| Function | What it does |
|----------|--------------|
| `decompress_msz()` | Top-level entry |
| `decompress_parallel()` | Spawns and joins workers |
| `decompress_routine()` | Per-thread worker |
| `read_footer()` | Parses the 88-byte footer (in `file.c`) |
