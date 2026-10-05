# Contributing

Working on `mscompress`. The repo has three buildable surfaces, all
backed by the same C core.

- [Development setup](setup.md) — cmake, uv, npm
- [Running tests](testing.md) — ctest, pytest, vitest
- [Memory leak workflow](memory-leaks.md) — Valgrind and macOS `leaks`
- [Versioning](versioning.md) — `.cz.toml` and `cz bump`
- [CI overview](ci.md) — what the build matrix covers

## Where things live

| Surface | Source | Tests |
|---------|--------|-------|
| C core | `src/`, `vendor/` | — |
| CLI | `cli/` | `cli/test/` (ctest) |
| Python | `python/mscompress/` | `python/test/` (pytest) |
| Node.js | `node-ts/src/` | `node-ts/test/` (vitest) |
| Electron app | `electron/` | — |

## Adding an algorithm

See the [implementer guide](../algorithms/implementing.md) — touches
`src/algos/`, `src/algo.c`, `src/mscompress.h`, and adds tests in all
three test suites.
