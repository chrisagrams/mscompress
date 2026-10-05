# Exit codes and errors

The CLI returns:

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Argument error, file not found, or unrecoverable processing error |

Errors are printed to stderr. Verbose mode (`-v` / `--verbose`)
prints additional progress information to stderr.

## Common error messages

### `Invalid mz lossy compression type.`

The name passed to `--mz-lossy` doesn't match a registered algorithm.
Run:

```bash
mscompress --list-algorithms
```

to see the valid names.

### `Unkown size suffix. (KB, MB, GB)`

`--blocksize` accepts a number with one of those suffixes — no others.
Examples: `100MB`, `2GB`, `512KB`.

### `Could not open input file`

Path doesn't exist or isn't readable. Check the path and permissions.

### Corrupt base64 / zlib stream errors

The input mzML contains malformed binary arrays. See
[Troubleshooting](../python/troubleshooting.md#corrupt-base64-or-invalid-base64-character-on-extraction) —
the same diagnostic applies across all interfaces.

## Reporting bugs

Include the output of `mscompress --version`, the offending command line,
and the relevant stderr output when filing issues at
[github.com/chrisagrams/mscompress/issues](https://github.com/chrisagrams/mscompress/issues).
