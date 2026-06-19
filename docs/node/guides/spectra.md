# Spectra

`Spectra` is the iterable collection of `Spectrum` instances exposed by
every file:

```ts
f.spectra;                  // Spectra
f.spectra[0];               // first Spectrum
f.spectra[42];              // 43rd Spectrum
f.spectra.length;           // number
for (const s of f.spectra) { /* in scan order */ }
```

## `Spectrum` properties

| Property | Type | Notes |
|----------|------|-------|
| `index` | number | Position in the file |
| `scan` | number | Original scan number |
| `msLevel` | number | 1, 2, ... |
| `retentionTime` | number | seconds |
| `size` | number | Peak count |
| `mz` | `Float32Array \| Float64Array` | Lazy — read triggers one block decompress |
| `intensity` | `Float32Array \| Float64Array` | Lazy |
| `xml` | string | Spectrum XML element |

`mz` and `intensity` are computed lazily on first access. Iterating
`f.spectra` without touching either is cheap; touching `s.mz` is a single
block decompression.

## Iteration patterns

```ts
// MS2 spectra
const ms2 = [...f.spectra].filter(s => s.msLevel === 2);

// A scan range
for (let i = 1000; i < 2000; i++) {
  const s = f.spectra[i];
  ...
}
```

For complex selections, prefer the [extraction guide](extracting.md) — it
pushes the filter into the C core.
