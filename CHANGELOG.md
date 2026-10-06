# Changelog

## [1.1.1](https://github.com/chrisagrams/mscompress/compare/v1.1.0...v1.1.1) (2026-10-06)


### Bug Fixes

* don't treat --extract onto an existing .mzML output as a batch request ([#206](https://github.com/chrisagrams/mscompress/issues/206)) ([b3cbd91](https://github.com/chrisagrams/mscompress/commit/b3cbd9133d2466cf58a1d9538f03174c86bad6e7))
* extract the full scan list and the final mzML spectrum ([#205](https://github.com/chrisagrams/mscompress/issues/205)) ([bea9c84](https://github.com/chrisagrams/mscompress/commit/bea9c848a0d2e91d3db9112d5e9ddc8f10653a8b))
* zlib re-encode of &gt;64 KB arrays corrupts output or segfaults ([#196](https://github.com/chrisagrams/mscompress/issues/196)) ([df6f5a0](https://github.com/chrisagrams/mscompress/commit/df6f5a04920953a8ac6b0631260510b44ea630d1))

## [1.1.0](https://github.com/chrisagrams/mscompress/compare/v1.0.16...v1.1.0) (2026-10-05)


### Features

* add --json flag for CLI informational commands ([22f2760](https://github.com/chrisagrams/mscompress/commit/22f27603aa79f1f7999da9098da98b395b7c6e2f)), closes [#130](https://github.com/chrisagrams/mscompress/issues/130)
* add --list-algorithms CLI flag to print available lossy transforms ([6c1a12c](https://github.com/chrisagrams/mscompress/commit/6c1a12c2fabd30943ff1670d13f53c1227beb50f)), closes [#126](https://github.com/chrisagrams/mscompress/issues/126)
* add JAX dataloader (mirrors PyTorch dataset) ([d1ff915](https://github.com/chrisagrams/mscompress/commit/d1ff9151729782c052698c2237f686984d5eff69))
* add new test cases for list_algorithms ([7e34b98](https://github.com/chrisagrams/mscompress/commit/7e34b983cfb928ef1c1562730c334f84def1ad21))
* add on-the-fly spectrum transforms to Python bindings ([f4af231](https://github.com/chrisagrams/mscompress/commit/f4af2318cfee134bfa34b790e92f1e5a282ea4b8)), closes [#122](https://github.com/chrisagrams/mscompress/issues/122)
* **batch:** extract a shared incremental .mszx writer API ([1ae1927](https://github.com/chrisagrams/mscompress/commit/1ae19278175ca54c0af57d676f74df962530b73c))
* **bindings:** add batch (.mszx v2) read and write support ([f998a49](https://github.com/chrisagrams/mscompress/commit/f998a4955cfd40fc252c95bb8805336498e00a5d))
* **bindings:** add byte-shuffle support to Python and node-ts ([65abbcc](https://github.com/chrisagrams/mscompress/commit/65abbcc38d6beff7dc5395a0c0d4a9c20b637a3c))
* **bindings:** reject unsupported msz versions at open in Python and node-ts ([3b785cb](https://github.com/chrisagrams/mscompress/commit/3b785cbd7d075a2b6d9c627fec63e75e5f25a018))
* **bindings:** reject v2/batch .mszx in python & node readers ([376f77d](https://github.com/chrisagrams/mscompress/commit/376f77def5f4734303215b029b0c3bdeb5e3f9d2))
* **cli:** add optional byte-shuffle transform for binary streams ([0f1e921](https://github.com/chrisagrams/mscompress/commit/0f1e9215090e59de94deafaa0947467716a0cd0b))
* **cli:** batch mode — compress folder/glob/list into one .mszx ([d20727a](https://github.com/chrisagrams/mscompress/commit/d20727a5bb2b5791795788c90b3b9437c2a39a19))
* **cli:** enable the byte shuffle by default ([5e15a6e](https://github.com/chrisagrams/mscompress/commit/5e15a6e4926bcc150e8367dcfd23f1e8a2fe75a1))
* **core:** replace per-file LRU with a shared byte-budgeted BlockCache ([5954b35](https://github.com/chrisagrams/mscompress/commit/5954b35f8f3aef0b24bf9734a0ffcf84b8c39cca))
* expose algorithm registry in Python bindings with validation ([4a6e1e5](https://github.com/chrisagrams/mscompress/commit/4a6e1e5bc2fae646d1a014a82748001d2ecb04c5)), closes [#131](https://github.com/chrisagrams/mscompress/issues/131)
* **format:** bump msz format to 0.2, read 0.1 and 0.2 ([8c4217b](https://github.com/chrisagrams/mscompress/commit/8c4217bb5464eeed8d26589e7572ee45c02177e5))
* implement streaming methods for compression, decompression, and extraction in MZML and MSZ files ([6ae3e48](https://github.com/chrisagrams/mscompress/commit/6ae3e48b2080e51e6adff398556d9b77f411ca46))
* initial docs ([6457645](https://github.com/chrisagrams/mscompress/commit/64576452d995165683c83854bdb88b0f967c4f56))
* native MSZX support in C core + first-class Cython MSZXFile ([02889f1](https://github.com/chrisagrams/mscompress/commit/02889f1e9b74cfe26160f5c4f523217d912abded))
* **node-ts:** add MZMLFile.compressStream() for no-temp-file MSZ streaming ([8fefda4](https://github.com/chrisagrams/mscompress/commit/8fefda4800a14c2b8fec86148f110ce4bd6b38a3))
* parquet -&gt; MSZ/MSZX converter (Python bindings) ([468a4d0](https://github.com/chrisagrams/mscompress/commit/468a4d039fdeee613db026c52ad79d25472c97d1))
* **parquet:** accept flexible parquet schemas ([b566b3f](https://github.com/chrisagrams/mscompress/commit/b566b3fdcd2328e10233c5f984eddf47f280d9c6))
* **parquet:** support ThermoRawFileParser .mzparquet long-format input ([780ef0f](https://github.com/chrisagrams/mscompress/commit/780ef0fce412e52180fc8cc0ed2ba5ed889dec59)), closes [#144](https://github.com/chrisagrams/mscompress/issues/144)
* **parquet:** unified from_parquet API + ThermoRawFileParser .mzparquet support ([4a3f220](https://github.com/chrisagrams/mscompress/commit/4a3f220423a1e3129cfbc0cc70441504949cd04a))
* refactor parquet module ([4981fde](https://github.com/chrisagrams/mscompress/commit/4981fde5c174267f31826028f0a5e8b3ec4dad88))


### Bug Fixes

* **build:** restore -O3 for vendored zlib (silently dropped since v1.0.11) ([e6f559b](https://github.com/chrisagrams/mscompress/commit/e6f559b5ea41756f59d2045a419d980b36072161))
* **build:** use find_packages so new subpackages ship in wheels ([0041831](https://github.com/chrisagrams/mscompress/commit/0041831676458546ad52c7feb7ceea3a379fb4ad))
* **cli:** fix memory leaks (zlib inflate lifecycle, division, serialize_df buffer) ([cc4c670](https://github.com/chrisagrams/mscompress/commit/cc4c6707cf6e6efc4b729ffb1cd3e53420a293cd))
* **cli:** free per-file compressor state in batch loop to bound memory ([e258a0b](https://github.com/chrisagrams/mscompress/commit/e258a0b150c59a5b1c9e25310c37210b500dfebd))
* **cli:** implement Windows directory-walk and glob for batch mode ([34eb4d5](https://github.com/chrisagrams/mscompress/commit/34eb4d543fb9459bd65c7973a55045dd7f52d86e))
* **cli:** report build configuration instead of Dev in version status ([493d796](https://github.com/chrisagrams/mscompress/commit/493d796d3e1c5bc0f91a8f8a711f9e30071137a6))
* ensure proper cleanup order in MSZXFile.close() method ([06a0b5c](https://github.com/chrisagrams/mscompress/commit/06a0b5c5979b1d2cfc9ebfc34251094d2cd0c2ab))
* **extract:** gate no-encode fast path to lossless; fix lossy direct reads ([f00eae0](https://github.com/chrisagrams/mscompress/commit/f00eae01ebc3ee4c74be7d84b58aea426d4621ff)), closes [#164](https://github.com/chrisagrams/mscompress/issues/164)
* improve error handling in file operations ([ee7539f](https://github.com/chrisagrams/mscompress/commit/ee7539f218d7732280376796c97cfc218c793578))
* keep test fixtures byte-identical on Windows, and test that they are ([741df7f](https://github.com/chrisagrams/mscompress/commit/741df7fdd27010e35d769ea864e4100e81d7d332))
* **node-ts:** close file handles before temp cleanup in compress-stream tests (Windows EPERM) ([b1cef42](https://github.com/chrisagrams/mscompress/commit/b1cef42d44228f9bc349342b94525438585ae714))
* **node-ts:** make compressStream cross-platform (Windows pipe/handle crash) ([71ff3cb](https://github.com/chrisagrams/mscompress/commit/71ff3cb9cd7176d43cb1aece3bc3ea9931647b7f))
* **node-ts:** stream compressed bytes via native pull API (fix Windows worker crash) ([1714e4e](https://github.com/chrisagrams/mscompress/commit/1714e4ec2e45cee12906fe169b3c9ddac62b151b))
* **node-ts:** use a real pipe on all platforms in compressStream (net.Socket) ([1e35002](https://github.com/chrisagrams/mscompress/commit/1e35002460f66c6452685901e360054a76afeb3f))
* **python:** build wheels against numpy 2 ABI to enable cp313 prebuilt wheels ([a7bb6d6](https://github.com/chrisagrams/mscompress/commit/a7bb6d6d5a7992ec0cad0980d92d9a9fde2a0c1d))
* **python:** give binding its own inflate stream (fixes segfault from reused-stream change) ([00e7001](https://github.com/chrisagrams/mscompress/commit/00e7001ec3cb006c91b516271a71564a05fd38b6))
* **python:** release GIL during compress()/decompress() to avoid worker deadlock ([8ecf58a](https://github.com/chrisagrams/mscompress/commit/8ecf58a1180870de0649311f91f3ff4222d25a62))
* **python:** skip pytorch on macos x86_64; require torch &gt;= 2.3 elsewhere ([6b3e7d5](https://github.com/chrisagrams/mscompress/commit/6b3e7d56e92b0515decd786a23c8201418e90ec9))
* segfault on lossy compression of non-indexed mzML ([#176](https://github.com/chrisagrams/mscompress/issues/176)) ([9191794](https://github.com/chrisagrams/mscompress/commit/919179429932c93c4c7eedd27fdd0dee21f64cb5))
* **test:** make node batch temp-dir cleanup best-effort on Windows ([50de2b8](https://github.com/chrisagrams/mscompress/commit/50de2b8f35ca01c974039e8937bf23ff29661e1d))
* use Node 24 for npm trusted publisher OIDC support ([313fda4](https://github.com/chrisagrams/mscompress/commit/313fda47a424499ab7d0c7ae9e2780234ae5c4ac))
* validate scale factors and fix dangling char* in Python lossy setters ([60aa047](https://github.com/chrisagrams/mscompress/commit/60aa04730b3c02bb660a67784e95787b977777d2))
* **windows:** define ssize_t for MSVC in mszx.c ([187a4a1](https://github.com/chrisagrams/mscompress/commit/187a4a1992acd9ced68878aeb110b420be85e538))


### Performance Improvements

* **cli:** reuse per-thread inflate stream (init once/reset per block) ([fff9a95](https://github.com/chrisagrams/mscompress/commit/fff9a95d466662d58ac28f53a5eb82571252b1c1))
* **core:** resolve MSZ spectrum metadata lazily instead of flattening ([f98ee10](https://github.com/chrisagrams/mscompress/commit/f98ee10daa54e02fbfe1dd626df3ba133c87e622))
* **datasets:** lazy, per-worker MSCompressDataset for multi-worker DataLoader ([e42c9ec](https://github.com/chrisagrams/mscompress/commit/e42c9ec36f5be50f6ad105d15def94412da12adf))
* **extract:** no-encode fast path for Python lossless reads ([dc91b22](https://github.com/chrisagrams/mscompress/commit/dc91b224db7e5cee2a8431e657ce601e5b439873))
* **python:** bound Spectra object cache with an LRU dict ([0be2ad5](https://github.com/chrisagrams/mscompress/commit/0be2ad562a2d840510332648f65dec9f17664a1f))


### Code Refactoring

* centralize algorithm metadata into a registry table ([df4f8ef](https://github.com/chrisagrams/mscompress/commit/df4f8ef4167eea0af4ba84c4f3d4086115315045))
* **cli:** drop unused created_at from v2 batch manifest ([380cea6](https://github.com/chrisagrams/mscompress/commit/380cea637ddff46412d91f7dc37d8f9138f68275))
* **format:** gate the version check on decode, report it in --describe ([a2d67d2](https://github.com/chrisagrams/mscompress/commit/a2d67d27552ec36bbe89383284cd8fcc7adcf0b8))
* make MSZXFile inherit from MSZFile in Python bindings ([d23fc53](https://github.com/chrisagrams/mscompress/commit/d23fc53a487a7cf2321628b1d0649bc8a90c8829))
* **node-ts:** mirror Python MSZX zero-copy + first-class subclass ([f480ed3](https://github.com/chrisagrams/mscompress/commit/f480ed32759aea2a58a59b7c2420b765ce036404))
* **python:** expose block cache only via read(), not constructors ([331535b](https://github.com/chrisagrams/mscompress/commit/331535bb5cdf1d8eb10df6aa0451d5d66e4a036c))
* **python:** expose cache_spectra only via read(), not constructors ([8dfbd98](https://github.com/chrisagrams/mscompress/commit/8dfbd983eaf69fa92ea07a260db35196f10074cb))
* replace _last_error_message global with error buffer in validate_args ([3a3da96](https://github.com/chrisagrams/mscompress/commit/3a3da961ee824fc415e8f771832f519733f5d782))
* split algo.c into per-algorithm files under src/algos/ ([aa198c4](https://github.com/chrisagrams/mscompress/commit/aa198c4b3983af37c2be682a103d0e82007acef4)), closes [#121](https://github.com/chrisagrams/mscompress/issues/121)

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
