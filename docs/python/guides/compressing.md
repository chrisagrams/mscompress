# Compressing files

```python
import mscompress

with mscompress.read("in.mzML") as f:
    f.arguments.threads = 8
    f.arguments.zstd_compression_level = 9
    f.compress("out.msz")
```

## `RuntimeArguments`

All knobs live on `f.arguments` (a `RuntimeArguments` instance). The fields
that matter for most users:

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `threads` | int | auto (CPU count) | Worker thread count |
| `blocksize` | int | 100 MB | Bytes per ZSTD block |
| `zstd_compression_level` | int | 3 | 1 (fast) … 22 (max) |
| `mz_lossy` | str | `""` | Algorithm name for m/z transform |
| `int_lossy` | str | `""` | Algorithm name for intensity transform |
| `mz_scale_factor` | float | algorithm default | Quantization parameter |
| `int_scale_factor` | float | algorithm default | Quantization parameter |
| `target_mz_format` | str | `"zstd"` | `"zstd"` or `"none"` |
| `target_inten_format` | str | `"zstd"` | `"zstd"` or `"none"` |
| `target_xml_format` | str | `"zstd"` | `"zstd"` or `"none"` |

## Choosing algorithms

```python
# Lossless (default)
f.arguments.mz_lossy = ""
f.arguments.int_lossy = ""

# Balanced lossy — recommended for most pipelines
f.arguments.mz_lossy = "delta32"
f.arguments.int_lossy = "log"

# Aggressive
f.arguments.mz_lossy = "delta24"
f.arguments.int_lossy = "cast"
```

See [Choosing a profile](../../getting-started/choosing-a-profile.md) and
the [algorithm catalog](../../algorithms/catalog/index.md) for full
characterizations.

## Streaming compression

For very large inputs that won't fit comfortably in memory:

```python
f.compress_stream("out.msz")
```

The streaming compressor uses the same threading model but flushes blocks
incrementally instead of materializing the full output in memory.

## Listing available algorithms at runtime

```python
print(mscompress.list_algorithms())
```

Returns the registered algorithms with their default scale factors and
descriptions, sourced from the C `algo_registry`.
