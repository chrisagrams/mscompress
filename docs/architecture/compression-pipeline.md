# Compression pipeline

`compress.c` orchestrates compression with a worker-pool model. The entry
point is `compress_mzml()`; everything below is fanned out from there.

## Phases

1. **Scan** (`preprocess.c`) — `yxml` walks the mzML once, recording byte
   ranges for every spectrum and parsing the `data_format_t` (source m/z and
   intensity types, source compression).
2. **Divide** — split the spectrum range into N divisions (one per worker
   thread). Each `division_t` holds the per-spectrum byte ranges for XML,
   m/z, and intensity.
3. **Plan** — `set_compress_runtime_variables()` resolves the chosen
   algorithm + source data type to function pointers stored on
   `data_format_t`. Each worker reads from the shared `data_format_t`.
4. **Compress** — `compress_parallel()` spawns `N` threads of
   `compress_routine()`. Each thread:
    - Allocates its own `ZSTD_CCtx`.
    - Iterates spectra in its division.
    - For each spectrum: decode source base64/zlib → apply algorithm
      transform → encode (optional zlib + base64) → ZSTD compress the block.
    - Pushes the compressed block onto a `cmp_blk_queue_t`.
5. **Flush** — the main thread joins the workers and writes the three streams,
   their block-length metadata, the divisions table, and the footer.

## Threading model

- **Threads:** `arguments.threads`, or `get_num_threads()` (CPU count) by
  default.
- **Per-thread state:** one `ZSTD_CCtx`, one input cursor over its
  division, one output queue.
- **Shared state:** `data_format_t` (read-only after planning) and
  `divisions_t` (each thread owns one `division_t`).
- **Synchronization:** none on the hot path. Threads write to disjoint
  output queues, the main thread joins and serializes.

Platform glue lives in `src/sys.c`: `pthread_*` on POSIX, `CreateThread` +
`WaitForMultipleObjects` on Windows.

## Why three streams?

Each of XML, m/z, and intensity is compressed by its own dedicated pipeline.
The compressor's job is much easier on a stream of pure binary floats than
on text-and-floats interleaved, and on intensity-only data than on the
XML+m/z+intensity mix mzML uses.

The cost is that finding a single spectrum requires reading from three
streams, but the block-length metadata makes that O(1) seeks plus three
single-block decompressions.

## Entry points

| Function | What it does |
|----------|--------------|
| `compress_mzml()` | Top-level entry; runs scan → divide → plan → compress |
| `compress_parallel()` | Spawns and joins worker threads |
| `compress_routine()` | Per-thread worker — iterates one division |
| `cmp_binary_routine()` | Per-spectrum: decode → algo → encode → ZSTD |
| `write_header()` / `write_footer()` | Serialize MSZ structure (in `file.c`) |
