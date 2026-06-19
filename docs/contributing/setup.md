# Development setup

You can develop any one surface independently, but a contribution that
adds an algorithm or changes the format usually touches all three. Set
all of them up once.

## Clone with submodules

```bash
git clone --recurse-submodules https://github.com/chrisagrams/mscompress.git
cd mscompress
```

## C / CLI

```bash
cmake -S . -B .
cmake --build .
ctest --test-dir cli
```

See [CLI build from source](../cli/build-from-source.md) for toolchain
prereqs.

## Python

```bash
cd python
uv sync --all-extras --reinstall
uv run pytest
```

For debugging / Valgrind:

```bash
MSCOMPRESS_DEBUG=1 uv sync --all-extras --reinstall
```

See [Build modes](../python/build-modes.md).

## Node.js

```bash
cd node-ts
npm install
npm run build
npm test
```

## Pre-commit / formatting

The C code uses `clang-format`; Python uses `ruff`; TypeScript uses
`prettier` + `eslint`. Run formatters before opening a PR — the CI
checks them.

## Conventional commits

Commit messages should follow Conventional Commits (`feat:`, `fix:`,
`chore:`, etc.). `cz commit` interactively builds a valid message:

```bash
uv tool run commitizen commit
```
