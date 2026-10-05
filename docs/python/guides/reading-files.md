# Reading files

`mscompress.read()` is the one entry point you usually need. It detects the
file type and returns either an `MZMLFile` or an `MSZFile`:

```python
import mscompress

with mscompress.read("anything.mzML") as f:
    ...

with mscompress.read("anything.msz") as f:
    ...
```

Both file classes share the same `BaseFile` interface (see the
[reference](../reference/core.md)), so most code is type-agnostic.

## Direct construction

When you know the type up front:

```python
mzml = mscompress.MZMLFile("data.mzML")
msz  = mscompress.MSZFile("data.msz")
```

## File-level metadata

```python
with mscompress.read("data.msz") as f:
    print(f.path)            # path on disk
    print(f.filesize)        # bytes
    print(f.format)          # DataFormat — source/target encodings, scale factors
    print(len(f.spectra))    # total spectrum count
```

## Resource cleanup

Always use a `with` block. Spectra hold references back into the file's
memory mapping; reading them after the file is closed will raise.

## Format detection details

`read()` inspects the first few bytes of the file to find the MSZ magic tag
(`0x035F51B5`); anything else is assumed to be mzML and validated by the
parser as it reads. MSZX archives (`.mszx`) are opened with `MSZXFile`
directly, not via `read()`.
