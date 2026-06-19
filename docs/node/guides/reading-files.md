# `read()` and file handling

```ts
import { read, MZMLFile, MSZFile } from "mscompress";

const f = read("anything.mzML");  // returns MZMLFile
const g = read("anything.msz");   // returns MSZFile
```

`read()` inspects the file's magic bytes — the MSZ tag `0x035F51B5` — and
returns the right concrete class. Both extend `BaseFile`, so most code
can work against the shared surface.

## Direct construction

```ts
const mzml = new MZMLFile("data.mzML");
const msz  = new MSZFile("data.msz");
```

## File-level metadata

```ts
f.path;           // string
f.filesize;       // number — bytes
f.format;         // DataFormat
f.spectra.length; // number
```

## Resource cleanup

Always call `close()` when done:

```ts
const f = read("data.msz");
try {
  // ... use f ...
} finally {
  f.close();
}
```

Spectra hold references back into the file's mmap; reading them after
`close()` throws.

The bindings work with the
[`using` declaration](https://github.com/tc39/proposal-explicit-resource-management)
when your TypeScript target supports it:

```ts
using f = read("data.msz");
// ... f.close() is called automatically at scope exit ...
```
