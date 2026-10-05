# Vendor libraries

All third-party code is vendored under `vendor/`. No system dependencies
beyond a C/C++17 toolchain, Python, and CMake.

| Library | Role | Location |
|---------|------|----------|
| [zlib](https://github.com/cloudflare/zlib) (Cloudflare fork) | Deflate for the legacy mzML inner encoding | `vendor/zlib/` |
| [zstd](https://github.com/facebook/zstd) | Primary block compressor for all three streams | `vendor/zstd/` |
| [lz4](https://github.com/lz4/lz4) | Alternative compressor (faster, lower ratio) | `vendor/lz4/` |
| [base64](https://github.com/aklomp/base64) | SIMD-optimized base64 codec (AVX2, SSSE3, SSE4, NEON) | `vendor/base64/` |
| [yxml](https://github.com/getml/yxml) | Streaming XML parser for mzML scanning | `vendor/yxml/` |

The base64 library is what makes mzML scanning fast — base64 dominates the
profile on a typical mzML file, and the SIMD paths run at memory bandwidth.

The Cloudflare zlib fork is faster than upstream on x86_64 thanks to
SSE/CRC32 accelerations.
