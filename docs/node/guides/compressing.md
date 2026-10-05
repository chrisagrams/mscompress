# Compression

```ts
import { read } from "mscompress";

const f = read("in.mzML");
f.arguments.threads = 8;
f.arguments.zstdCompressionLevel = 9;
f.compress("out.msz");
f.close();
```

## `RuntimeArguments`

All compression knobs live on `f.arguments`:

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `threads` | number | auto (CPU count) | Worker thread count |
| `blocksize` | number | 100 MB | Bytes per ZSTD block |
| `zstdCompressionLevel` | number | 3 | 1 (fast) … 22 (max) |
| `mzLossy` | string | `""` | Algorithm name (`delta32`, ...) |
| `intLossy` | string | `""` | Algorithm name |
| `mzScaleFactor` | number | algorithm default | Quantization |
| `intScaleFactor` | number | algorithm default | Quantization |
| `targetMzFormat` | string | `"zstd"` | `"zstd"` or `"none"` |
| `targetIntenFormat` | string | `"zstd"` | `"zstd"` or `"none"` |
| `targetXmlFormat` | string | `"zstd"` | `"zstd"` or `"none"` |

## Lossy compression

```ts
f.arguments.mzLossy = "delta32";
f.arguments.intLossy = "log";
f.compress("out.msz");
```

See [Choosing a profile](../../getting-started/choosing-a-profile.md) and
the [algorithm catalog](../../algorithms/catalog/index.md) for what each
setting trades.
