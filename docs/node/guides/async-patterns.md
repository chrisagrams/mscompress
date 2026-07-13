# Async patterns

The native addon executes compression and decompression on libuv worker
threads, so calls don't block the event loop. The TypeScript API is
synchronous in shape (it doesn't return Promises), but the underlying
work runs in parallel with whatever else your event loop is doing.

For Promise-style parallelism, wrap the synchronous calls in `Promise.all`
across an array of files:

```ts
import { read } from "mscompress";

const files = ["a.mzML", "b.mzML", "c.mzML"];
await Promise.all(files.map(async path => {
  const f = read(path);
  try {
    f.compress(path.replace(".mzML", ".msz"));
  } finally {
    f.close();
  }
}));
```

## Resource cleanup

Always call `close()`. If your TypeScript target supports
[`using` declarations](https://github.com/tc39/proposal-explicit-resource-management)
(ES2024+), prefer them:

```ts
using f = read("data.msz");
// f.close() runs at scope exit
```

Otherwise, use `try { ... } finally { f.close(); }`.
