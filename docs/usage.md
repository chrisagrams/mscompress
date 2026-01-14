# Usage Guide

## Quick Start

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

## Working with Spectra

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

## Compression Configuration

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

## Random Access to Compressed Data

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

## Context Manager Support

```python
import mscompress

# Use with context manager for automatic resource cleanup
with mscompress.read("data.mzML") as file:
    for spectrum in file.spectra:
        # Process spectrum
        process_spectrum(spectrum)
# File is automatically closed
```
