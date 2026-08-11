# Build from source

Most users don't need this — `npm install mscompress` downloads a
prebuilt binary. Source builds are needed for unsupported platforms,
debugging, or when contributing.

## Prerequisites

- Node.js ≥ 16
- Python 3 (for `node-gyp` / `cmake-js`)
- C/C++17 toolchain
  - macOS: Xcode Command Line Tools
  - Linux: `gcc`/`clang` + `make`
  - Windows: Visual Studio Build Tools 2022+
- CMake ≥ 3.15

## Build

```bash
cd node-ts
npm install
npm run build
```

This runs:

1. `prebuild-install -r napi` (postinstall) — try to fetch a prebuilt
   binary.
2. `npm run build` — `tsc && tsc-alias`.

If the prebuild fetch fails, `cmake-js` is invoked automatically.

## Debug build

```bash
npm run build:debug
```

Produces a binary with debug symbols and `-O0`. Use before attaching a
debugger.

## Run tests

```bash
npm test            # vitest run
npm run test:watch  # watch mode
```

Tests are in `test/` and pull fixtures from the repo-root `test/data/`.

## TypeScript reference autogen

To generate a typedoc-style API reference into `docs/node/reference/`:

```bash
npx typedoc --plugin typedoc-plugin-markdown \
            --out ../docs/node/reference/generated \
            src/index.ts
```

This isn't wired into the current docs build — it's a planned addition.
See the [reference index](reference/index.md).
