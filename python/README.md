# MScompress Python Library

Python bindings for MScompress, a high-performance compression library for mass spectrometry data.

## Overview

The MScompress Python library provides a fast and efficient way to compress and decompress mass spectrometry data files in mzML format. It features:

- 🚀 **High Performance**: Multi-threaded compression/decompression with state-of-the-art speeds
- 📦 **MSZ Format**: Novel compressed format with random-access capabilities
- 🔄 **Lossless & Lossy**: Support for both lossless and lossy compression modes
- 🐍 **Pythonic API**: Clean, intuitive interface with NumPy integration
- 🎯 **Direct Data Access**: Extract spectra, m/z arrays, and intensity data without full decompression

## Installation

### From PyPI (Coming Soon)

```bash
pip install mscompress
```

### From Source

**Prerequisites:**
- Python ≥ 3.8
- NumPy
- Cython
- C compiler (GCC, Clang, or MSVC)

**Build and install:**

```bash
git clone --recurse-submodules https://github.com/chrisagrams/mscompress.git
cd mscompress/python
pip install -e .
```

## Quick Start

### Basic Usage

```python
import mscompress

# Read an mzML file
mzml = mscompress.read("data.mzML")

# Compress to MSZ format
mzml.compress("data.msz")

# Read compressed MSZ file
msz = mscompress.read("data.msz")

# Decompress back to mzML
msz.decompress("output.mzML")
```

### Working with Spectra

```python
import mscompress

# Open a file (mzML or MSZ)
file = mscompress.read("data.mzML")

# Access file metadata
print(f"File size: {file.filesize} bytes")
print(f"Total spectra: {len(file.spectra)}")

# Iterate through all spectra
for spectrum in file.spectra:
    print(f"Scan {spectrum.scan}: MS{spectrum.ms_level}")
    print(f"  Retention time: {spectrum.retention_time:.2f}s")
    print(f"  Data points: {spectrum.size}")

# Access specific spectrum by index
spectrum = file.spectra[0]

# Get m/z and intensity arrays as NumPy arrays
mz_array = spectrum.mz
intensity_array = spectrum.intensity

# Get combined peaks as 2D array [m/z, intensity]
peaks = spectrum.peaks

# Access spectrum metadata XML
xml_element = spectrum.xml
```

### Compression Configuration

```python
import mscompress

# Open mzML file
mzml = mscompress.read("data.mzML")

# Configure compression settings
mzml.arguments.threads = 8  # Use 8 threads
mzml.arguments.zstd_compression_level = 3  # Set ZSTD level (1-22)
mzml.arguments.blocksize = 100  # Spectra per block

# Compress with custom settings
mzml.compress("data.msz")
```

### Random Access to Compressed Data

```python
import mscompress

# Open compressed MSZ file
msz = mscompress.read("data.msz")

# Directly access specific spectrum without full decompression
spectrum_100 = msz.spectra[100]
print(f"Scan: {spectrum_100.scan}")
print(f"m/z range: {spectrum_100.mz[0]:.2f} - {spectrum_100.mz[-1]:.2f}")

# Extract binary data for a specific spectrum
mz_binary = msz.get_mz_binary(100)
intensity_binary = msz.get_inten_binary(100)
xml_metadata = msz.get_xml(100)
```

### Context Manager Support

```python
import mscompress

# Use with context manager for automatic resource cleanup
with mscompress.read("data.mzML") as file:
    for spectrum in file.spectra:
        # Process spectrum
        process_spectrum(spectrum)
# File is automatically closed
```

## API Reference

### Functions

#### `read(path: str | bytes) -> MZMLFile | MSZFile`
Opens and parses an mzML or MSZ file.

**Parameters:**
- `path`: Path to the file (string or bytes)

**Returns:**
- `MZMLFile` or `MSZFile` object depending on file type

**Raises:**
- `FileNotFoundError`: If file does not exist
- `IsADirectoryError`: If path points to a directory
- `OSError`: If file type cannot be determined

#### `get_num_threads() -> int`
Returns the number of available CPU threads.

#### `get_filesize(path: str | bytes) -> int`
Returns the size of a file in bytes.

### Classes

#### `MZMLFile`
Handler for mzML format files.

**Properties:**
- `path`: File path (bytes)
- `filesize`: File size in bytes (int)
- `format`: Data format information (DataFormat)
- `spectra`: Collection of spectra (Spectra)
- `positions`: Division/position information (Division)
- `arguments`: Runtime configuration (RuntimeArguments)

**Methods:**
- `compress(output: str | bytes)`: Compress to MSZ format
- `get_mz_binary(index: int) -> np.ndarray`: Extract m/z array for spectrum
- `get_inten_binary(index: int) -> np.ndarray`: Extract intensity array for spectrum
- `get_xml(index: int) -> Element`: Extract XML metadata for spectrum
- `describe() -> dict`: Get file description dictionary

#### `MSZFile`
Handler for MSZ (compressed) format files.

**Properties:**
- Same as `MZMLFile`

**Methods:**
- `decompress(output: str | bytes)`: Decompress to mzML format
- `get_mz_binary(index: int) -> np.ndarray`: Extract m/z array for spectrum
- `get_inten_binary(index: int) -> np.ndarray`: Extract intensity array for spectrum
- `get_xml(index: int) -> Element`: Extract XML metadata for spectrum
- `describe() -> dict`: Get file description dictionary

#### `Spectrum`
Represents a single mass spectrum.

**Properties:**
- `index`: Spectrum index (int)
- `scan`: Scan number (int)
- `ms_level`: MS level (int)
- `retention_time`: Retention time in seconds (float)
- `size`: Number of data points (int)
- `mz`: m/z values (np.ndarray)
- `intensity`: Intensity values (np.ndarray)
- `peaks`: Combined m/z and intensity as 2D array (np.ndarray)
- `xml`: XML metadata element (Element)

#### `Spectra`
Collection of spectra with lazy loading and iteration support.

**Methods:**
- `__len__()`: Get total number of spectra
- `__iter__()`: Iterate over all spectra
- `__getitem__(index: int)`: Access spectrum by index

#### `RuntimeArguments`
Runtime configuration for compression/decompression.

**Properties:**
- `threads`: Number of threads to use (int)
- `blocksize`: Number of spectra per block (int)
- `mz_scale_factor`: m/z scaling factor (int)
- `int_scale_factor`: Intensity scaling factor (int)
- `target_xml_format`: Target XML format (int)
- `target_mz_format`: Target m/z format (int)
- `target_inten_format`: Target intensity format (int)
- `zstd_compression_level`: ZSTD compression level 1-22 (int)

#### `DataFormat`
Data format information.

**Properties:**
- `source_mz_fmt`: Source m/z format (int)
- `source_inten_fmt`: Source intensity format (int)
- `source_compression`: Source compression type (int)
- `source_total_spec`: Total number of spectra (int)
- `target_xml_format`: Target XML format (int)
- `target_mz_format`: Target m/z format (int)
- `target_inten_format`: Target intensity format (int)

**Methods:**
- `to_dict() -> dict`: Convert to dictionary representation
- `__str__() -> str`: String representation

#### `Division`
Division structure containing data positions and scan information.

**Properties:**
- `spectra`: Spectrum data positions (DataPositions)
- `xml`: XML data positions (DataPositions)
- `mz`: m/z data positions (DataPositions)
- `inten`: Intensity data positions (DataPositions)
- `size`: Number of divisions (int)
- `scans`: Scan numbers (np.ndarray)
- `ms_levels`: MS levels (np.ndarray)
- `ret_times`: Retention times (np.ndarray or None)

#### `DataPositions`
Position information for data blocks.

**Properties:**
- `start_positions`: Start positions (np.ndarray[uint64])
- `end_positions`: End positions (np.ndarray[uint64])
- `total_spec`: Total number of spectra (int)

## Examples

### Extract All MS2 Spectra

```python
import mscompress
import numpy as np

file = mscompress.read("data.mzML")

ms2_spectra = []
for spectrum in file.spectra:
    if spectrum.ms_level == 2:
        ms2_spectra.append({
            'scan': spectrum.scan,
            'rt': spectrum.retention_time,
            'mz': spectrum.mz,
            'intensity': spectrum.intensity
        })

print(f"Found {len(ms2_spectra)} MS2 spectra")
```

### Compare Compression Ratios

```python
import mscompress
import os

mzml = mscompress.read("data.mzML")
original_size = mzml.filesize

# Compress with different ZSTD levels
for level in [1, 3, 5, 10]:
    mzml.arguments.zstd_compression_level = level
    output = f"data_level_{level}.msz"
    mzml.compress(output)
    
    compressed_size = os.path.getsize(output)
    ratio = original_size / compressed_size
    print(f"Level {level}: {ratio:.2f}x compression")
```

### Parallel Processing of Spectra

```python
import mscompress
from multiprocessing import Pool

def process_spectrum(args):
    file_path, index = args
    file = mscompress.read(file_path)
    spectrum = file.spectra[index]
    # Your processing logic here
    return spectrum.scan, len(spectrum.mz)

file = mscompress.read("data.mzML")
indices = range(len(file.spectra))
args = [(file.path.decode(), i) for i in indices]

with Pool() as pool:
    results = pool.map(process_spectrum, args)

print(f"Processed {len(results)} spectra")
```

### Filter and Extract Peaks

```python
import mscompress
import numpy as np

file = mscompress.read("data.mzML")

# Extract peaks above intensity threshold
threshold = 1000.0

for spectrum in file.spectra:
    mask = spectrum.intensity > threshold
    filtered_mz = spectrum.mz[mask]
    filtered_intensity = spectrum.intensity[mask]
    
    print(f"Scan {spectrum.scan}: {len(filtered_mz)} peaks above threshold")
```

## Performance Tips

1. **Multi-threading**: Set `arguments.threads` to match your CPU core count for optimal performance
2. **Block size**: Adjust `arguments.blocksize` based on your data - larger blocks may improve compression ratio but reduce random-access granularity
3. **Compression level**: ZSTD levels 1-3 offer good speed/ratio balance; higher levels improve compression at the cost of speed
4. **Memory usage**: When processing large files, iterate through spectra rather than loading all into memory

## Type Hints

The library includes full type hints and stub files (`.pyi`) for improved IDE support and type checking:

```python
import mscompress
from typing import Union

def process_file(path: Union[str, bytes]) -> None:
    file = mscompress.read(path)  # Type checker knows this is MZMLFile | MSZFile
    spectra = file.spectra  # Type: Spectra
    spectrum = spectra[0]  # Type: Spectrum
    mz = spectrum.mz  # Type: np.ndarray[np.float32 | np.float64]
```

## Contributing

Contributions are welcome! Please see the main repository for guidelines:
https://github.com/chrisagrams/mscompress

## License

See the main repository for license information.

## Support

For bug reports and feature requests, please open an issue:
https://github.com/chrisagrams/mscompress/issues

## Citation

If you use MScompress in your research, please cite our work (citation information coming soon).
