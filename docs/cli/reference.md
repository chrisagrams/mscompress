# Command reference

```
Usage: mscompress [OPTION...] input_file [output_file]
```

## Compression options

| Flag | Long | Argument | Default | Description |
|------|------|----------|---------|-------------|
| `-t` | `--threads` | num | auto | Worker thread count |
| `-z` | `--mz-lossy` | algorithm | — | m/z transform: `cast`, `cast16`, `delta16`, `delta24`, `delta32`, `bitpack`, `vbr`, `vdelta16`, `vdelta24` |
| `-i` | `--int-lossy` | algorithm | — | Intensity transform: `cast`, `log`, `vbr`, ... |
| — | `--mz-scale-factor` | float | algorithm default | Quantization parameter for m/z transform |
| — | `--int-scale-factor` | float | algorithm default | Quantization parameter for intensity transform |
| — | `--target-xml-format` | `zstd` \| `none` | `zstd` | XML stream compression |
| — | `--target-mz-format` | `zstd` \| `none` | `zstd` | m/z stream compression |
| — | `--target-inten-format` | `zstd` \| `none` | `zstd` | Intensity stream compression |
| — | `--zstd-compression-level` | 1–22 | 3 | ZSTD level |
| `-b` | `--blocksize` | size | 100MB | Block size — accepts `KB`/`MB`/`GB` suffixes |
| `-c` | `--checksum` | — | off | Generate MD5 checksum (not yet implemented) |

## Extraction options

| Flag | Argument | Description |
|------|----------|-------------|
| `--extract` | — | Force extraction mode (defaults are auto-detected from other extract flags) |
| `--extract-indices` | range | `0-100` or `[0-100]` |
| `--extract-scans` | range | `1-3,5-6` or `[1-3,5-6]` |
| `--ms-level` | int | 1, 2, n |

## Inspection options

| Flag | Long | Description |
|------|------|-------------|
| `-d` | `--describe` | Print header/footer (CSV by default; JSON with `--json`) |
| — | `--list-algorithms` | List available lossy algorithms |
| — | `--json` | Output JSON for `--describe`, `--list-algorithms`, `--version` |

## Generic options

| Flag | Long | Description |
|------|------|-------------|
| `-v` | `--verbose` | Verbose logging |
| `-h` | `--help` | Help and exit |
| `-V` | `--version` | Version and exit |

## Examples

### Lossless compression at max ratio

```bash
mscompress -t 8 --zstd-compression-level 22 in.mzML out.msz
```

### Balanced lossy for ML pipelines

```bash
mscompress --mz-lossy delta32 --int-lossy log in.mzML out.msz
```

### Extract all MS2 spectra to a new mzML

```bash
mscompress --ms-level 2 in.msz ms2.mzML
```

### Pull a scan range

```bash
mscompress --extract-scans 1000-2000 in.msz subset.mzML
```

### Print the on-disk format as JSON

```bash
mscompress --describe --json in.msz | jq
```
