# MSCompress Python Library

Python bindings for MSCompress, a high-performance compression library for mass spectrometry data.

## Features

- 🚀 **High Performance**: Multi-threaded compression/decompression with state-of-the-art speeds
- 📦 **MSZ Format**: Novel compressed format with random-access capabilities
- 🔄 **Lossless & Lossy**: Support for both lossless and lossy compression modes
- 🐍 **Pythonic API**: Clean, intuitive interface with NumPy integration
- 🎯 **Direct Data Access**: Extract spectra, m/z arrays, and intensity data without full decompression

## Installation

### From PyPI

```bash
pip install mscompress
```

### From Source

**Prerequisites:**
- Python ≥ 3.9
- NumPy
- Cython
- C compiler (GCC, Clang, or MSVC)

**Build and install:**

```bash
git clone --recurse-submodules https://github.com/chrisagrams/mscompress.git
cd mscompress/python
pip install -e .
```