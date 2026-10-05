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

## Batch options

Compress many mzML into one `.mszx` archive. See the
[MSZX format spec](../format/mszx.md) for the v2 layout.

| Flag | Long | Argument | Description |
|------|------|----------|-------------|
| — | `--batch` | — | Force batch mode (usually inferred — see below) |
| `-r` | `--recursive` | — | Descend into subdirectories for directory inputs |
| `-o` | `--output` | path | Output archive path. Required output form for batch |
| — | `--from-file` | path | Read newline-separated input paths from a manifest (`-` = stdin) |
| — | `--continue-on-error` | — | Skip an unusable input instead of aborting |
| — | `--list` | — | Print a `.mszx` table of contents and exit |

### When batch mode is inferred

You rarely need `--batch`. It is inferred when any of these hold:

- `--batch` or `--from-file` is given
- `-o` names a `.mszx` file
- any positional is a directory or contains a glob metacharacter (`*?[`)
- two or more positionals are existing `.mzML` files (e.g. a shell-expanded
  `*.mzML`)
- `-r`/`--recursive` is given

The legacy two-positional form `mscompress input output` is never batch: the
`.mzML` extension test above is what keeps it that way even when the output file
already exists from a previous run.

### Error handling

Batch mode is fail-fast by default: any unusable input aborts the run and the
partial archive is removed.

`--continue-on-error` relaxes that for inputs rejected *before* compression
starts — a file that cannot be opened, mapped, or is not mzML is skipped, and
the archive is still written with the remaining entries. A failure *during* an
entry cannot be rescued: its bytes are already in the archive and cannot be
un-appended, so the run still aborts and removes the output.

The output must be a seekable regular file. A pipe, FIFO, or stdout is a hard
error — see [why](../format/mszx.md#why-v2-output-must-be-seekable).

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

### Compress a folder of mzML into one archive

```bash
mscompress --batch runs/ -o cohort.mszx
mscompress runs/ -r -o cohort.mszx          # recurse into subdirectories
mscompress runs/*.mzML -o cohort.mszx       # shell-expanded, batch inferred
```

### Compress an explicit list

```bash
find runs -name '*.mzML' | mscompress --from-file - -o cohort.mszx
```

### Inspect and expand an archive

```bash
mscompress --list cohort.mszx               # table of contents
mscompress cohort.mszx out/                 # expand every member to out/*.mzML
```
