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
  MSZXBatchFile,
  MSZXBatchWriter,
  MSZXBatchManifest,
  compressBatch,
  type AnnotationEntry,
  type MSZXManifestData,
  createMSZX,

  // Search results (stored in archives as mzTab)
  MzTabReader,
  PSMCollection,
  writeMzTab,
  readPSMs,
  toMzTab,
  convertToMzTab,
  parsePepXML,
  parsePercolator,
  detectAnnotationFormat,
  mzTabMemberName,
  createPSM,
  createSearchInfo,
  type PSM,
  type SearchInfo,
  type ModificationSpec,

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

Reader for `.mszx` archives. `open()`, `manifest`, `spectra`,
`annotation_files`, `getAnnotationFile()`, `getAnnotationFilesByFormat()`,
`getPSMReader()`, `psmReaders`, `annotationReader`, `psms`, `extract()`,
`close()`. Archives written before mzTab became the only stored format
(raw pepXML / Percolator members) read through the same accessors.

### `MSZXBuilder`

Fluent builder. `new MSZXBuilder(msz, sourceName?)`, `addAnnotations()`
(converts to mzTab), `addPSMs()`, `addMzTab()`, `setDescription()`,
`setJoinKey()` (default `"spectra_ref"`), `setExtra()`, `save()`,
`saveSync()`.

### `MSZXManifest`

Manifest record for a v1 archive. See the
[MSZX format spec](../../format/mszx.md).

### `MSZXBatchFile`

Reader for a v2 ("batch") archive, as a lazily-opened collection of `MSZFile`.
Also opens a v1 archive as a collection of one.

`open()`, `length`, `entries`, `names`, `get(index | name)`, `has()`,
`decompress()`, `describe()`, `close()`, iterable, `Symbol.dispose`.

### `MSZXBatchWriter`

Incremental writer for a v2 archive. Drives the same C writer as the CLI, so
output is byte-identical.

`add(source, entryName?)` (async), `addPSMs()` (converts to mzTab),
`addAnnotation()`, `setJoinKey()`,
`setDescription()`, `setExtra()`, `finish()`, `abort()`, `entries`, `length`.

### `compressBatch(inputs, output, options?)`

Compress many mzML into one `.mszx`. Options: `recursive`, `description`,
`extra`, `onProgress`, plus any `CompressArgs`.

### `resolveMzmlInputs(inputs, recursive?)`

Expand paths and directories into the sorted, deduplicated mzML list
`compressBatch` would use.

### `MSZXBatchManifest`

Manifest record for a v2 archive. `spectra_files`, `description`, `extra`,
`fromSingleFile()`.

### `createMSZX(msz, outputPath, options?)`

Convenience function. `options.annotations` is a list of file paths, each
converted to mzTab. See the [MSZX guide](../guides/mszx.md).

## Search results

MSZX archives store PSM annotations only as mzTab 1.0; see the
[format spec](../../format/mszx.md#search-results-mztab).

### `PSMCollection`

Parsed PSMs with a query API: `psms`, `length`, `at()`, `getByScan()`,
`getBestByScan()`, `hasScan()`, `scanNumbers`, `searchInfo`, `format`,
`sourceFormat`, `sourceName`, iterable.

### `MzTabReader extends PSMCollection`

`new MzTabReader(data: string | Buffer)`. Adds `metadata` (the `MTD` map),
`text`, and `filter(scanNumbers: Set<number>): string`, which returns the
document with only the PSM rows of those scans.

### `writeMzTab(psms, options?)`

Serialize PSMs to mzTab text. Options: `searchInfo`, `msRunLocation`,
`sourceFormat`, `sourceName`, `title`.

### `readPSMs(data, nameHint?, format?, options?)`

Parse mzTab, pepXML or Percolator TSV/PIN (zstd buffers are decompressed;
format auto-detected unless given) into a `PSMCollection`.

### `toMzTab(data, nameHint?, options?)` / `convertToMzTab(data, nameHint?, options?)`

Convert any supported input to mzTab text; mzTab input is returned unchanged.
`convertToMzTab` also returns `numRecords` and `inputFormat`. Options:
`format`, `msRunLocation`, `sourceName`, `title`, `searchInfo`, plus parser
options.

### `parsePepXML(data, options?)` / `parsePercolator(data, options?)`

Engine-specific parsers returning `{ psms, searchInfo }`. Options: `minRank`,
`decoyPrefix` (pepXML); `decoyPrefix` (Percolator).

### `detectAnnotationFormat(text, nameHint?)` / `mzTabMemberName(name, compressed, isMzTab?)`

The format-detection and member-naming rules (`<name>.mztab`, plus `.zst`).

### `createPSM(fields)` / `createSearchInfo(fields?)`

Build a `PSM` / `SearchInfo` with every unspecified field defaulted.

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
