# Node.js Quick Start

## Install

```bash
npm install mscompress
```

The install script downloads a prebuilt N-API addon for your platform.
On unsupported platforms or in air-gapped environments, the install
falls back to `cmake-js` and builds from source — see [Build from
source](build-from-source.md) for the toolchain requirements.

## Read an mzML file

```ts
import { read } from "mscompress";

const f = read("sample.mzML");
console.log(`${f.spectra.length} spectra in ${f.filesize} bytes`);

for (const s of f.spectra) {
  console.log(s.scan, s.msLevel, s.retentionTime, s.size);
}

f.close();
```

`read()` auto-detects mzML vs MSZ and returns the right file object.
The `spectra` collection is lazy — iterating doesn't materialize every
binary array.

## Get the binary data

```ts
const s = f.spectra[42];
const mz = s.mz;            // Float32Array | Float64Array
const intensity = s.intensity;
```

Typed arrays are returned directly without intermediate copies.

## Compress to MSZ

```ts
const f = read("sample.mzML");
f.arguments.threads = 8;
f.arguments.zstdCompressionLevel = 9;
f.compress("sample.msz");
f.close();
```

## Random-access reads on MSZ

```ts
const f = read("huge.msz");
const s = f.spectra[100_000];   // single block decompressed
console.log(s.mz.slice(0, 5));
f.close();
```

## Lossy compression

```ts
f.arguments.mzLossy = "delta32";
f.arguments.intLossy = "log";
f.compress("sample.lossy.msz");
```

See [Choosing a profile](../getting-started/choosing-a-profile.md) and
the [algorithm catalog](../algorithms/catalog/index.md).

## What's next

- [Spectra](guides/spectra.md) — iteration, properties, async patterns
- [Filtered extraction](guides/extracting.md)
- [MSZX archives](guides/mszx.md)
