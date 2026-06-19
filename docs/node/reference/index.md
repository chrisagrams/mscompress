# Node.js API Reference

The TypeScript surface is small and cohesive. For a generated reference
matching the published types, run `typedoc` (see
[Build from source](../build-from-source.md)); for a navigable view, see
the source under `node-ts/src/`.

## Module surface

```ts
import {
  // Files
  BaseFile,
  MZMLFile,
  MSZFile,

  // Spectra
  Spectrum,
  Spectra,

  // Types
  DataFormat,
  DataPositions,
  Division,
  RuntimeArguments,
  type ExtractOptions,

  // MSZX
  MSZXFile,
  MSZXBuilder,
  MSZXManifest,
  type AnnotationEntry,
  type MSZXManifestData,
  createMSZX,

  // Module functions
  read,
  getNumThreads,
  getFilesize,
  getVersion,
} from "mscompress";
```

## Files

### `read(path: string): MZMLFile | MSZFile`

Auto-detects the file type and returns the appropriate file object. See
[`read()` and file handling](../guides/reading-files.md).

### `BaseFile`

Common base class. Methods: `compress()`, `decompress()`, `extract()`,
`close()`. Properties: `path`, `filesize`, `format`, `spectra`,
`positions`, `arguments`.

### `MZMLFile extends BaseFile`

Handler for uncompressed mzML files.

### `MSZFile extends BaseFile`

Handler for MSZ files. Supports random-access reads.

## Spectra

### `Spectrum`

Single mass spectrum. Properties: `index`, `scan`, `msLevel`,
`retentionTime`, `size`, `mz`, `intensity`, `xml`.

### `Spectra`

Iterable, indexable collection. `length`, `[i]`, `[Symbol.iterator]`.

## Types

### `DataFormat`

Source and target encoding metadata: `sourceMzFmt`, `sourceIntenFmt`,
`sourceCompression`, `targetXmlFormat`, `targetMzFormat`,
`targetIntenFormat`, `mzScaleFactor`, `intScaleFactor`.

### `DataPositions`

Start/end byte positions and spectrum counts for a data block.

### `Division`

Per-worker spectrum partition: `spectra`, `xml`, `mz`, `inten`
(`DataPositions`), `scans`, `msLevels`, `retTimes`.

### `RuntimeArguments`

Compression configuration. See the [compression guide](../guides/compressing.md)
for every field.

### `ExtractOptions`

```ts
type ExtractOptions = {
  indices?: number[];
  scanNumbers?: number[];
  msLevel?: number;
};
```

## MSZX

### `MSZXFile`

Reader for `.mszx` archives. `manifest`, `spectra`, `openAnnotation()`,
`close()`.

### `MSZXBuilder`

Fluent builder. `setSpectra()`, `setDescription()`, `setJoinKey()`,
`addAnnotation()`, `setExtra()`, `save()`.

### `MSZXManifest`

Manifest record. See the [MSZX format spec](../../format/mszx.md).

### `createMSZX(options)`

Convenience function. See the [MSZX guide](../guides/mszx.md).

## Module functions

### `getNumThreads(): number`

Available CPU count.

### `getFilesize(path: string): number`

File size in bytes.

### `getVersion(): string`

Library version (from `.cz.toml`).

!!! note
    A future revision of these docs will autogen this page with
    `typedoc-plugin-markdown` so signatures stay in lock-step with the
    source. See [Build from source](../build-from-source.md) for the
    planned workflow.
