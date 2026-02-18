# mscompress

Multi-threaded lossless/lossy compression library for Mass Spectrometry data with Node.js/TypeScript bindings.

[![npm version](https://badge.fury.io/js/mscompress.svg)](https://www.npmjs.com/package/mscompress)
[![Tests](https://github.com/chrisagrams/mscompress/actions/workflows/build.yml/badge.svg)](https://github.com/chrisagrams/mscompress/actions)

## Features

- 🚀 **Fast**: Multi-threaded compression/decompression using ZSTD
- 🎯 **Random Access**: Read individual spectra without full decompression
- 🔧 **Flexible**: Supports both lossless and lossy compression
- 📊 **Format Conversion**: mzML ↔ MSZ with filtering options
- 🗂️ **MSZX Archives**: Bundle MSZ files with annotations and metadata
- 🔒 **Type-Safe**: Full TypeScript support with type definitions

## Installation

```bash
npm install mscompress
```

Pre-built binaries are available for:
- macOS (x64, ARM64)
- Linux (x64, ARM64)
- Windows (x64)

## Quick Start

```typescript
import { read, MZMLFile, MSZFile } from 'mscompress';

// Auto-detect and open file
const file = read('sample.mzML');

// Compress mzML to MSZ
const msz = file.compress('output.msz');

// Access spectra
for (const spectrum of file.spectra) {
  console.log(`Scan ${spectrum.scan}: ${spectrum.size} peaks`);
  console.log(`m/z range: ${spectrum.mz[0]} - ${spectrum.mz[spectrum.mz.length - 1]}`);
  console.log(`Retention time: ${spectrum.retentionTime}s`);
}

// Decompress MSZ back to mzML
const mzml = msz.decompress('output.mzML');

// Extract filtered data
msz.extract('ms2-only.mzML', { msLevel: 2 });
msz.extract('first-10.mzML', { indices: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9] });
msz.extract('scans.mzML', { scanNumbers: [100, 200, 300] });

// Always close files
file.close();
```

## API Reference

### `read(path: string): MZMLFile | MSZFile`

Auto-detects file type and returns the appropriate file handle.

```typescript
const file = read('sample.mzML');  // Returns MZMLFile
const msz = read('sample.msz');    // Returns MSZFile
```

### `MZMLFile`

Handles uncompressed mzML files.

#### Methods

- **`compress(outputPath: string): MSZFile`** - Compress to MSZ format
- **`extract(output: string, options?: ExtractOptions): MZMLFile | MSZFile`** - Extract with filters
- **`getMzBinary(index: number): Float32Array | Float64Array`** - Get m/z array for spectrum
- **`getIntenBinary(index: number): Float32Array | Float64Array`** - Get intensity array
- **`getXml(index: number): string`** - Get spectrum XML
- **`close(): void`** - Close file and free resources
- **`describe(): object`** - Get file metadata

#### Properties

- **`path: string`** - File path
- **`filesize: number`** - File size in bytes
- **`format: DataFormat`** - Data format information
- **`positions: Division`** - Spectrum positions
- **`spectra: Spectra`** - Iterable spectrum collection
- **`arguments: RuntimeArguments`** - Compression settings

### `MSZFile`

Handles compressed MSZ files.

#### Methods

- **`decompress(outputPath: string): MZMLFile`** - Decompress to mzML format
- **`extract(output: string, options?: ExtractOptions): MZMLFile | MSZFile`** - Extract with filters
- **`getMzBinary(index: number): Float32Array | Float64Array`** - Get m/z array for spectrum
- **`getIntenBinary(index: number): Float32Array | Float64Array`** - Get intensity array
- **`getXml(index: number): string`** - Get spectrum XML
- **`close(): void`** - Close file and free resources
- **`describe(): object`** - Get file metadata

#### Properties

Same as `MZMLFile`.

### `MSZXFile`

Handles bundled MSZX archives containing MSZ files with annotations and metadata.

#### Static Methods

- **`open(filePath: string): MSZXFile`** - Open an MSZX archive

#### Methods

- **`close(): void`** - Close archive and clean up temporary files
- **`getAnnotationFile(filename: string): Buffer`** - Get raw annotation file content
- **`getAnnotationFilesByFormat(format: string): string[]`** - Get annotation files by format
- **`extractMSZX(output: string, options?: ExtractOptions): Promise<MSZXFile>`** - Extract subset to new MSZX

All MSZ file methods are also available (getMzBinary, getIntenBinary, getXml, decompress, extract).

#### Properties

- **`archive_path: string`** - Path to the MSZX archive
- **`manifest: MSZXManifest`** - Archive manifest with metadata
- **`annotation_files: AnnotationEntry[]`** - List of annotation files
- All MSZ file properties (path, filesize, format, positions, spectra, arguments)

### `MSZXBuilder`

Builder for creating MSZX archives.

```typescript
const msz = read('sample.msz') as MSZFile;
const builder = new MSZXBuilder(msz);

builder
  .setDescription('Proteomics dataset')
  .addAnnotations('results.pin', { format: 'percolator_tsv' })
  .setExtra('experiment_id', 'EXP001');

await builder.save('sample.mszx');
```

#### Methods

- **`addAnnotations(filePath: string, options?): this`** - Add annotation file
- **`setDescription(description: string): this`** - Set archive description
- **`setJoinKey(joinKey: string): this`** - Set join key (default: 'scan_number')
- **`setExtra(key: string, value: any): this`** - Add custom metadata
- **`save(outputPath: string): Promise<string>`** - Save archive

### `MSZXManifest`

Manifest describing MSZX archive contents.

#### Properties

- **`version: string`** - Manifest version
- **`created_at: string`** - Creation timestamp (ISO 8601)
- **`spectra_file: string`** - Name of the MSZ file
- **`num_spectra: number`** - Number of spectra
- **`annotations: AnnotationEntry[]`** - List of annotation files
- **`join_key: string`** - Key for joining spectra with annotations
- **`description?: string`** - Archive description
- **`source_file?: string`** - Original source file name
- **`extra: Record<string, any>`** - Custom metadata

#### Methods

- **`toString(indent?: number): string`** - Serialize to JSON string
- **`toJSON(): object`** - Convert to plain object
- **`static parse(json: string): MSZXManifest`** - Parse from JSON string

### `Spectrum`

Represents a single mass spectrum.

#### Properties

- **`index: number`** - Spectrum index in file
- **`scan: number`** - Scan number from instrument
- **`msLevel: number`** - MS level (1 for MS1, 2 for MS/MS, etc.)
- **`retentionTime: number | null`** - Retention time in seconds
- **`size: number`** - Number of m/z-intensity pairs
- **`mz: Float32Array | Float64Array`** - m/z values (lazy-loaded)
- **`intensity: Float32Array | Float64Array`** - Intensity values (lazy-loaded)
- **`peaks: Float64Array`** - Interleaved [m/z, intensity] pairs
- **`xml: string`** - Spectrum XML (lazy-loaded)

### `Spectra`

Iterable collection of spectra.

```typescript
const spectra = file.spectra;

// Get length
console.log(spectra.length);

// Access by index
const spectrum = spectra.get(0);

// Iterate
for (const spectrum of spectra) {
  // Process spectrum
}
```

### `RuntimeArguments`

Configure compression settings.

```typescript
const file = read('sample.mzML');
file.arguments.threads = 8;
file.arguments.zstdCompressionLevel = 5;
const msz = file.compress('output.msz');
```

#### Properties

- **`threads: number`** - Number of threads (default: CPU count)
- **`blocksize: number`** - Block size for division (default: 100 MB)
- **`zstdCompressionLevel: number`** - ZSTD level 1-22 (default: 3)
- **`targetXmlFormat: number`** - XML compression format
- **`targetMzFormat: number`** - m/z compression format
- **`targetIntenFormat: number`** - Intensity compression format

### `ExtractOptions`

Options for filtering during extraction.

```typescript
interface ExtractOptions {
  indices?: number[];        // Extract specific spectrum indices
  scanNumbers?: number[];    // Extract specific scan numbers
  msLevel?: number;          // Extract only spectra at this MS level
}
```

### Utility Functions

- **`getNumThreads(): number`** - Get system CPU count
- **`getFilesize(path: string): number`** - Get file size in bytes
- **`getVersion(): string`** - Get mscompress version

## Examples

### Compress with Custom Settings

```typescript
import { read } from 'mscompress';

const mzml = read('sample.mzML');
mzml.arguments.threads = 16;
mzml.arguments.zstdCompressionLevel = 9;
mzml.arguments.blocksize = 50_000_000; // 50 MB blocks

const msz = mzml.compress('output.msz');
console.log(`Compression ratio: ${mzml.filesize / msz.filesize}x`);
```

### Extract MS2 Spectra Only

```typescript
import { read } from 'mscompress';

const msz = read('sample.msz');
const ms2File = msz.extract('ms2-only.mzML', { msLevel: 2 });

console.log(`Extracted ${ms2File.spectra.length} MS2 spectra`);
```

### Process Spectra

```typescript
import { read } from 'mscompress';

const file = read('sample.msz');

for (const spectrum of file.spectra) {
  if (spectrum.msLevel === 1) {
    // Find base peak
    let maxIntensity = 0;
    let basePeakMz = 0;

    for (let i = 0; i < spectrum.size; i++) {
      if (spectrum.intensity[i] > maxIntensity) {
        maxIntensity = spectrum.intensity[i];
        basePeakMz = spectrum.mz[i];
      }
    }

    console.log(`Scan ${spectrum.scan}: Base peak at ${basePeakMz.toFixed(4)} m/z`);
  }
}

file.close();
```

### Batch Conversion

```typescript
import { read } from 'mscompress';
import { readdirSync } from 'fs';
import { join } from 'path';

const inputDir = './mzml-files';
const outputDir = './msz-files';

for (const filename of readdirSync(inputDir)) {
  if (!filename.endsWith('.mzML')) continue;

  const inputPath = join(inputDir, filename);
  const outputPath = join(outputDir, filename.replace('.mzML', '.msz'));

  const mzml = read(inputPath);
  console.log(`Compressing ${filename}...`);
  mzml.compress(outputPath);
  mzml.close();
}
```

### Working with MSZX Archives

#### Create an MSZX Archive

```typescript
import { read, createMSZX, MSZFile } from 'mscompress';

const msz = read('sample.msz') as MSZFile;

// Using convenience function
await createMSZX(msz, 'sample.mszx', {
  description: 'Proteomics dataset with PSM annotations',
  annotations: ['results.pin', 'results.pepXML'],
  extra: {
    experiment_id: 'EXP001',
    instrument: 'Orbitrap Fusion',
  },
});

msz.close();
```

#### Using MSZXBuilder

```typescript
import { read, MSZXBuilder, MSZFile } from 'mscompress';

const msz = read('sample.msz') as MSZFile;
const builder = new MSZXBuilder(msz);

builder
  .setDescription('Annotated proteomics dataset')
  .setJoinKey('scan_number')
  .addAnnotations('percolator.pin', {
    format: 'percolator_tsv',
    description: 'Percolator PSM results',
  })
  .addAnnotations('results.pepXML', {
    format: 'pepxml',
    description: 'X!Tandem search results',
  })
  .setExtra('experiment', 'EXP001')
  .setExtra('date', '2024-01-15');

await builder.save('annotated.mszx');
msz.close();
```

#### Read an MSZX Archive

```typescript
import { MSZXFile } from 'mscompress';

const mszx = MSZXFile.open('sample.mszx');

// Access manifest
console.log(mszx.manifest.description);
console.log(`Spectra: ${mszx.manifest.num_spectra}`);
console.log(`Annotations: ${mszx.annotation_files.length}`);

// Access spectra (same as MSZFile)
for (const spectrum of mszx.spectra.slice(0, 10)) {
  console.log(`Scan ${spectrum.scan}: ${spectrum.size} peaks`);
}

// Access annotation files
for (const entry of mszx.annotation_files) {
  console.log(`${entry.filename}: ${entry.format} (${entry.num_records} records)`);
  const content = mszx.getAnnotationFile(entry.filename);
  // Process annotation content...
}

mszx.close();
```

#### Extract Subset to New MSZX

```typescript
import { MSZXFile } from 'mscompress';

const mszx = MSZXFile.open('sample.mszx');

// Extract MS2 spectra with annotations to new archive
const ms2Archive = await mszx.extractMSZX('ms2-only.mszx', {
  msLevel: 2,
});

console.log(`Extracted ${ms2Archive.spectra.length} MS2 spectra`);
ms2Archive.close();
mszx.close();
```

## Platform Support

The `mscompress` package uses native bindings and pre-built binaries for optimal performance. The main package automatically installs the correct platform-specific binary:

- `@mscompress/darwin-x64` - macOS Intel
- `@mscompress/darwin-arm64` - macOS Apple Silicon
- `@mscompress/linux-x64` - Linux x64
- `@mscompress/linux-arm64` - Linux ARM64
- `@mscompress/win32-x64` - Windows x64

If your platform is not supported, the package will attempt to build from source (requires C++ compiler and CMake).

## Building from Source

```bash
git clone https://github.com/chrisagrams/mscompress.git
cd mscompress/node-ts
npm install
npm run build
npm test
```

### Requirements

- Node.js >= 16
- C++17 compiler (GCC, Clang, or MSVC)
- CMake >= 3.15
- Python 3 (for node-gyp)

## Related Projects

- [mscompress Python](https://pypi.org/project/mscompress/) - Python bindings for the same C library
- [mscompress CLI](https://github.com/chrisagrams/mscompress) - Command-line tool

## License

MIT © Chris Grams

## Citation

If you use mscompress in your research, please cite:

```bibtex
@software{mscompress2024,
  author = {Grams, Chris},
  title = {mscompress: Multi-threaded compression for mass spectrometry data},
  year = {2024},
  url = {https://github.com/chrisagrams/mscompress}
}
```

## Support

- 📖 [Documentation](https://github.com/chrisagrams/mscompress)
- 🐛 [Issue Tracker](https://github.com/chrisagrams/mscompress/issues)
- 💬 [Discussions](https://github.com/chrisagrams/mscompress/discussions)
