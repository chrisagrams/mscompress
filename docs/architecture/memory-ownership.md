# Memory ownership rules

These are the rules that cause leaks, double-frees, and use-after-frees
when broken. Keep them next to you when writing new C code.

## Two division ownership modes

`division_t` (and the wrapping `divisions_t`) appears on two code paths
with **different ownership**:

- **Compress path** — allocated via `malloc` during the scan phase.
  Free with `dealloc_division()` / `dealloc_divisions()`.
- **Decompress / read path** — pointers into the mmap'd file. The struct
  wrapper is `malloc`'d, but the data it points at is owned by the mmap.
  Free with `dealloc_read_division()` / `dealloc_read_divisions()`, which
  frees the wrapper only.

Mismatching these calls a `free` on mmap-backed pointers (crash) or leaves
allocated wrappers behind (leak).

## `encode_base64()` consumes its input

`encode_base64()` internally frees the `zlib_block_t*` you pass in. **Never
free it yourself afterward.** This is the most common new-contributor leak:
seeing the input pointer in the calling scope and trying to free it.

## `zlib_pop_header()` returns malloc'd memory

The 4-byte header prepended by `zlib_pop_header()` is a fresh `malloc`.
Save the pointer, use it, free it:

```c
ZLIB_TYPE* hdr = zlib_pop_header(&data);
// ... use hdr ...
free(hdr);
```

## Encode functions advance src pointers

Loops in `encode.c` modify `char** src` in place to walk through input.
**Save the original pointer before the loop** if you'll need it for cleanup:

```c
char* original_src = src;
encode_fun(z, &src, src_len, dest, &dest_len);
// src has advanced; use original_src for free()
```

## `block_len_t` caching

Before decompressing a block, check the cache:

```c
if (!blk->cache) {
    blk->cache = zstd_decompress(...);
}
```

This is a correctness rule, not just a performance optimization — multiple
calls to a decompress helper without the guard will leak the previous cache
allocation.

## Buffer ownership in `algo_args`

When implementing an algorithm, `args->src` and `args->dest` are **caller-owned**.
The algorithm function:

- Must not `free` either pointer.
- Must not store either pointer anywhere outside the call.
- Must respect `src_len` as a hard upper bound on reads.

See [Implementing a new algorithm](../algorithms/implementing.md) for the
full contract.

## Valgrind workflow

The Python package has a debug build mode that makes Valgrind output
readable. See [Memory leak workflow](../contributing/memory-leaks.md).
