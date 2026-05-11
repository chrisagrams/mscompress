# CLI Quick Start

After [building](build-from-source.md), the binary is at `./cli/mscompress`.

## Compress

```bash
mscompress input.mzML output.msz
```

If the output path is omitted, the input filename with the `.msz`
extension is used:

```bash
mscompress input.mzML        # → input.msz
```

## Decompress

```bash
mscompress input.msz output.mzML
```

The input file's extension determines the direction — `mscompress`
detects mzML vs MSZ from the file's magic bytes, not the extension.

## Extract a subset

```bash
mscompress --extract-indices 0-100 input.msz subset.mzML
mscompress --extract-scans 1-3,5-6 input.msz subset.mzML
mscompress --ms-level 2 input.msz ms2-only.mzML
```

## Inspect a file

```bash
mscompress --describe input.msz
mscompress --describe --json input.msz
```

## List available algorithms

```bash
mscompress --list-algorithms
mscompress --list-algorithms --json
```

## Lossy compression

```bash
mscompress --mz-lossy delta32 --int-lossy log input.mzML
mscompress --mz-lossy delta32 --mz-scale-factor 524288 input.mzML
```

See the [profile picker](../getting-started/choosing-a-profile.md) and the
[algorithm catalog](../algorithms/catalog/index.md).
