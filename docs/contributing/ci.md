# CI overview

GitHub Actions workflow at `.github/workflows/build.yml`. Five jobs run
on every PR; three more run on tagged releases.

## Per-PR

| Job | What it does |
|-----|--------------|
| `build-cli` | CMake build + ctest on Linux, Windows, macOS (x86_64, arm64) |
| `build-python` | `cibuildwheel` for CPython 3.9–3.13 + pytest |
| `build-node` | `prebuild` + `cmake-js` + vitest on Linux, macOS, Windows (x64, arm64) |

## On version tags

| Job | What it does |
|-----|--------------|
| `publish-python` | Upload wheels to PyPI |
| `publish-node` | Publish to npm; upload prebuilds to the GitHub Release |
| `build-docker` | Multi-arch Docker image |

## What the matrix catches

- ABI mismatches across Python versions
- Compiler differences (MSVC vs gcc vs clang) — especially around
  SIMD flags for the base64 vendor library
- Linker issues on arm64
- TypeScript regressions in `node-ts`

## Adding to CI

The build matrix is defined in `build.yml`. Adding a new toolchain or
target generally means:

1. Add an entry to the matrix.
2. Update the per-platform fixture paths if needed.
3. Verify locally with `act` (or just push a draft PR).
