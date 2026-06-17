# Changelog

All notable changes to this project are documented in this file. Going forward it is
maintained automatically by [release-please](https://github.com/googleapis/release-please)
from [Conventional Commits](https://www.conventionalcommits.org/). Entries for v1.0.16 and
earlier were backfilled from the project's [GitHub Releases](https://github.com/chrisagrams/mscompress/releases);
`chore`/`ci`/`test`/`docs` changes and release-bump pull requests are omitted to match
release-please's configured changelog sections, and prerelease/post CI iterations are
consolidated.

## [1.0.16](https://github.com/chrisagrams/mscompress/compare/v1.0.15...v1.0.16) (2026-05-18)


### Features

* **parquet:** unified `from_parquet` API + ThermoRawFileParser `.mzparquet` support ([#145](https://github.com/chrisagrams/mscompress/pull/145))
* native MSZX support in C core + first-class Cython `MSZXFile` ([#151](https://github.com/chrisagrams/mscompress/pull/151))
* add JAX dataloader (mirrors PyTorch dataset) ([#143](https://github.com/chrisagrams/mscompress/pull/143))


### Bug Fixes

* **python:** release GIL during `compress()`/`decompress()` to avoid worker deadlock ([#154](https://github.com/chrisagrams/mscompress/pull/154))
* **python:** build wheels against numpy 2 ABI to unblock cp313 on Windows ([#157](https://github.com/chrisagrams/mscompress/pull/157))
* handle empty `<binary></binary>` arrays in per-spectrum extraction ([#146](https://github.com/chrisagrams/mscompress/pull/146))


### Code Refactoring

* **node-ts:** mirror Python MSZX zero-copy + first-class subclass ([#152](https://github.com/chrisagrams/mscompress/pull/152))

## [1.0.15](https://github.com/chrisagrams/mscompress/compare/v1.0.14...v1.0.15) (2026-05-06)


### Features

* **parquet:** accept flexible parquet schemas ([#140](https://github.com/chrisagrams/mscompress/pull/140))

## [1.0.14](https://github.com/chrisagrams/mscompress/compare/v1.0.13...v1.0.14) (2026-05-05)


### Features

* parquet → MSZ/MSZX converter (Python bindings) ([#136](https://github.com/chrisagrams/mscompress/pull/136))
* expose algorithm registry in Python bindings with validation ([#133](https://github.com/chrisagrams/mscompress/pull/133))
* add on-the-fly spectrum transforms in Python bindings ([#129](https://github.com/chrisagrams/mscompress/pull/129))
* add `--list-algorithms` CLI flag ([#128](https://github.com/chrisagrams/mscompress/pull/128))
* add `--json` flag for CLI informational commands ([#132](https://github.com/chrisagrams/mscompress/pull/132))


### Code Refactoring

* split `algo.c` into per-algorithm files under `src/algos/` ([#125](https://github.com/chrisagrams/mscompress/pull/125))

## [1.0.13](https://github.com/chrisagrams/mscompress/compare/v1.0.12...v1.0.13) (2026-03-25)


### Bug Fixes

* bound `get_scan()` to fix O(N*S) extraction performance ([#110](https://github.com/chrisagrams/mscompress/pull/110))
* fix segfault on corrupt base64 and harden MSZ extraction ([#112](https://github.com/chrisagrams/mscompress/pull/112))
* fix all compilation warnings in non-vendor code ([#113](https://github.com/chrisagrams/mscompress/pull/113))

## [1.0.12](https://github.com/chrisagrams/mscompress/compare/v1.0.11...v1.0.12) (2026-03-24)


### Bug Fixes

* fix segfault in `get_scan()` for mzML without a `scan=` attribute ([#104](https://github.com/chrisagrams/mscompress/pull/104))
* fix swapped m/z and intensity dtypes ([#106](https://github.com/chrisagrams/mscompress/pull/106))

## [1.0.11](https://github.com/chrisagrams/mscompress/compare/v1.0.10...v1.0.11) (2026-02-23)

*Maintenance release: fix the Docker build GitHub Action ([#97](https://github.com/chrisagrams/mscompress/pull/97)).*

## [1.0.10](https://github.com/chrisagrams/mscompress/compare/v1.0.9...v1.0.10) (2026-02-18)


### Features

* add streaming response support for compress, decompress, and extract ([#92](https://github.com/chrisagrams/mscompress/pull/92))
* drop Python 3.9 support, add Python 3.14 ([#96](https://github.com/chrisagrams/mscompress/pull/96))


### Bug Fixes

* extract to a unique temporary directory to avoid collisions ([#86](https://github.com/chrisagrams/mscompress/pull/86))
* correct return types on failure paths ([#87](https://github.com/chrisagrams/mscompress/pull/87))


### Code Refactoring

* rewrite the Node.js library in TypeScript ([#93](https://github.com/chrisagrams/mscompress/pull/93))
* remove global `fds`/`fd_pos`, use scoped FD management ([#91](https://github.com/chrisagrams/mscompress/pull/91))

## [1.0.9](https://github.com/chrisagrams/mscompress/compare/v1.0.8...v1.0.9) (2026-02-12)


### Bug Fixes

* call MSZ cleanup in `MSZXFile.close()` ([#74](https://github.com/chrisagrams/mscompress/pull/74))


### Code Refactoring

* remove `debug.c` and its references ([#80](https://github.com/chrisagrams/mscompress/pull/80))

## [1.0.8](https://github.com/chrisagrams/mscompress/compare/v1.0.7...v1.0.8) (2026-02-10)


### Performance Improvements

* new build configuration — Python library ~17–28% faster in spectra loading ([#72](https://github.com/chrisagrams/mscompress/pull/72))


### Bug Fixes

* memory-leak fixes in the Python library, significantly reducing runtime memory ([#67](https://github.com/chrisagrams/mscompress/pull/67))
* update spectrum list count on extract ([#62](https://github.com/chrisagrams/mscompress/pull/62))

## [1.0.7.post2](https://github.com/chrisagrams/mscompress/compare/v1.0.7.post1...v1.0.7.post2) (2026-01-29)

*Maintenance release: fix CI for Linux aarch64 ([#64](https://github.com/chrisagrams/mscompress/pull/64)).*

## [1.0.7.post1](https://github.com/chrisagrams/mscompress/compare/v1.0.7...v1.0.7.post1) (2026-01-20)

*Maintenance release: add CTests and publish binaries on release ([#59](https://github.com/chrisagrams/mscompress/pull/59), [#60](https://github.com/chrisagrams/mscompress/pull/60)).*

## [1.0.7](https://github.com/chrisagrams/mscompress/compare/v1.0.6...v1.0.7) (2026-01-14)


### Features

* MSZX format support ([#56](https://github.com/chrisagrams/mscompress/pull/56))
* spectrum extraction in the Python bindings ([#55](https://github.com/chrisagrams/mscompress/pull/55))

## [1.0.6](https://github.com/chrisagrams/mscompress/compare/v1.0.5...v1.0.6) (2026-01-12)


### Bug Fixes

* correct retention-time handling ([#53](https://github.com/chrisagrams/mscompress/pull/53))

## [1.0.5](https://github.com/chrisagrams/mscompress/compare/v1.0.4...v1.0.5) (2025-12-10)


### Features

* support path-like arguments ([#51](https://github.com/chrisagrams/mscompress/pull/51))


### Bug Fixes

* fix early-stop condition ([#44](https://github.com/chrisagrams/mscompress/pull/44))
* fix extract CLI arguments ([#45](https://github.com/chrisagrams/mscompress/pull/45))

## [1.0.4](https://github.com/chrisagrams/mscompress/compare/v1.0.3...v1.0.4) (2025-12-02)


### Code Refactoring

* Python bindings refactor ([#42](https://github.com/chrisagrams/mscompress/pull/42))

## [1.0.3](https://github.com/chrisagrams/mscompress/compare/v1.0.2...v1.0.3) (2025-12-02)

*Maintenance release: repository cleanup ([#39](https://github.com/chrisagrams/mscompress/pull/39)).*

## [1.0.2](https://github.com/chrisagrams/mscompress/compare/v1.0.1a4...v1.0.2) (2025-11-11)


### Bug Fixes

* improve error handling in file operations ([#37](https://github.com/chrisagrams/mscompress/pull/37))

## [1.0.1a0](https://github.com/chrisagrams/mscompress/compare/v1.0.0-prerelease...v1.0.1a0) (2025-11-06)


### Features

* add Python library bindings ([#23](https://github.com/chrisagrams/mscompress/pull/23))


### Bug Fixes

* Python build fix ([#24](https://github.com/chrisagrams/mscompress/pull/24))

## 1.0.0-prerelease (2024-04-10)

Initial public prerelease of MScompress: multi-threaded lossless/lossy compression for
Mass Spectrometry data, the random-access `.msz` file format, and the command-line tool.
