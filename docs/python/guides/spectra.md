# Working with spectra

`Spectra` is the lazy, iterable collection of `Spectrum` objects hanging off
every file:

```python
with mscompress.read("data.msz") as f:
    f.spectra              # Spectra
    f.spectra[0]           # Spectrum (first)
    f.spectra[42]          # Spectrum (42nd)
    len(f.spectra)         # int
    for s in f.spectra:    # iteration is in scan order
        ...
```

## Spectrum properties

| Property | Type | Notes |
|----------|------|-------|
| `index` | int | Position in the file |
| `scan` | int | Original scan number |
| `ms_level` | int | 1, 2, ... |
| `retention_time` | float | seconds |
| `size` | int | Number of m/z–intensity pairs |
| `mz` | `numpy.ndarray` | 1-D, dtype depends on source encoding |
| `intensity` | `numpy.ndarray` | 1-D |
| `peaks` | `numpy.ndarray` | (N, 2) stack of m/z and intensity |
| `raw_mz` | bytes | Compressed/encoded raw bytes from the file |
| `raw_intensity` | bytes | Same, for intensity |
| `xml` | str | Spectrum XML element |

`mz`, `intensity`, and `peaks` are computed lazily on first access — opening
a 10 GB MSZ file is cheap; iterating its spectra without touching `mz` is
cheap; reading `s.mz` triggers exactly one block decompression.

## Transforms

Attach a callable to apply per-spectrum filtering or scaling at read time:

```python
def threshold(spectrum):
    mask = spectrum.intensity > 1e3
    return spectrum.mz[mask], spectrum.intensity[mask]

f.spectra.set_transform(threshold)
for s in f.spectra:
    mz, intensity = s.peaks  # already filtered
```

`with_transform()` returns a new view without mutating the parent:

```python
filtered = f.spectra.with_transform(threshold)
unfiltered = f.spectra
```

## Iteration patterns

```python
# All spectra
for s in f.spectra:
    ...

# Just MS2
ms2 = (s for s in f.spectra if s.ms_level == 2)

# A scan range
for i in range(1000, 2000):
    s = f.spectra[i]
```

For complex selections, prefer the [extraction guide](extracting.md) — it
pushes the filter down into the C core.
