# Native addon internals

The TypeScript classes wrap a thin N-API addon. If you're debugging,
extending the bindings, or porting to another runtime, this is where
to look.

## Binding surface

`src/core/bindings.ts` loads `build/Release/mscompress.node` and exposes
the raw N-API functions. The public TypeScript API wraps every one of
these.

### File operations

```ts
new native.FileHandle(path: string)
handle.close()
handle.filesize
handle.filetype
handle.isOpen
```

### Module functions

```ts
native.getFilesize(path: string): number
native.getNumThreads(): number
native.getVersion(): string
native.openOutputFile(path: string): FileHandle
native.closeOutputFile(handle: FileHandle): void
```

### Format detection / planning

```ts
native.getDataFormat(handle): DataFormatLike
native.setCompressRuntimeVars(handle, runtimeArgs): void
native.setDecompressRuntimeVars(handle, footer): void
```

### Position scanning

```ts
native.scanMzml(handle): { positions, metadata }
native.readMszDivisions(handle): Division[]
native.prepareDivisions(handle, args): Division[]
```

### Spectrum binary access

```ts
// mzML
native.getMzBinaryMzml(handle, spectrumIdx): Float32Array | Float64Array
native.getIntenBinaryMzml(handle, spectrumIdx): Float32Array | Float64Array
native.getXmlMzml(handle, spectrumIdx): string

// MSZ
native.getMzBinaryMsz(handle, spectrumIdx): Float32Array | Float64Array
native.getIntenBinaryMsz(handle, spectrumIdx): Float32Array | Float64Array
native.getXmlMsz(handle, spectrumIdx): string
```

### Compression / decompression

```ts
native.compressMzml(handle, outputPath, args): void
native.decompressMsz(handle, outputPath): void
native.extractMzmlFiltered(handle, outputPath, opts): void
native.extractMsz(handle, outputPath, opts): void
```

### Raw zstd utilities

```ts
native.zstdCompress(input: Buffer, level: number): Buffer
native.zstdDecompress(input: Buffer): Buffer
```

## Source layout

| File | Role |
|------|------|
| `src/native/addon.cpp` | Module entry, function registration |
| `src/native/file_ops.cpp` | `FileHandle` class, file I/O |
| `src/native/format.cpp` | Format detection |
| `src/native/spectrum.cpp` | Spectrum binary access |
| `src/native/division.cpp` | Division calculation |
| `src/native/compress.cpp` | Compression wrappers |
| `src/native/decompress.cpp` | Decompression wrappers |
| `src/native/extract.cpp` | Filtered extraction |
| `src/native/zstd_utils.cpp` | Raw zstd helpers |

All C++ files call into the shared C core in the repo-root `src/` —
the native addon is a thin shim, not a re-implementation.

## Prebuild flow

`npm publish` runs `prebuild` to compile the addon for every supported
target and uploads the binaries as GitHub Release assets. When a user
runs `npm install mscompress`, the install hook (`prebuild-install -r napi`)
downloads the matching binary instead of compiling.

If `prebuild-install` fails (unsupported platform, network issue,
mismatched Node ABI), it falls back to `cmake-js compile`, which requires
a C++17 compiler and Python 3 on the user's machine.
