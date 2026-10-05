# Build from source

## Prerequisites

- CMake ≥ 3.15
- A C/C++17 toolchain
  - macOS: Xcode Command Line Tools
  - Linux: `gcc` or `clang` + `make`
  - Windows: Visual Studio Build Tools 2022+
- `git` (for cloning with submodules)

No system dependencies — all third-party libraries (zlib, zstd, lz4,
base64, yxml) are vendored under `vendor/`.

## Clone

```bash
git clone --recurse-submodules https://github.com/chrisagrams/mscompress.git
cd mscompress
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build

```bash
cmake -S . -B .
cmake --build .
```

The binary is produced at `./cli/mscompress`.

## Test

```bash
ctest --test-dir cli
```

Run a single test by name:

```bash
ctest --test-dir cli -R CompressTest
ctest --test-dir cli -R "MZLossy_delta32"
```

## Install (optional)

The build does not install the binary to a system location by default.
Copy it manually:

```bash
sudo cp cli/mscompress /usr/local/bin/
```

Or run from the build tree.

## Debug build

```bash
cmake -S . -B . -DCMAKE_BUILD_TYPE=Debug
cmake --build .
```

Then attach `lldb` / `gdb` / `valgrind` to the resulting binary.
