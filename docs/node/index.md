# Node.js / TypeScript

N-API addon with TypeScript bindings. ESM-only, targets ES2022.
Prebuilt binaries are published for macOS (x64/arm64), Linux
(x64/arm64), and Windows (x64); `cmake-js` falls back to a source build
where prebuilds aren't available.

- **[Quick Start](quickstart.md)** — read mzML, iterate spectra,
  compress.
- **[Guides](guides/index.md)** — task-oriented recipes.
- **[API Reference](reference/index.md)** — generated from TSDoc.
- **[Native addon internals](native-internals.md)** — what the N-API
  layer exposes.
- **[Build from source](build-from-source.md)** — `cmake-js`,
  prerequisites, debug build.

## At a glance

```ts
import { read, getVersion } from "mscompress";

console.log(getVersion());

const f = read("data.mzML");
for (const s of f.spectra) {
  console.log(s.scan, s.msLevel, s.mz.length);
}

f.arguments.threads = 8;
f.arguments.zstdCompressionLevel = 9;
f.compress("data.msz");
f.close();
```
