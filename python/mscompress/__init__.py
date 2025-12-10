"""A versatile compression tool for efficient management of mass-spectrometry data."""

__version__ = "1.0.5"

from ._core import (
    RuntimeArguments,
    DataFormat,
    DataPositions,
    Division,
    BaseFile,
    MZMLFile,
    MSZFile,
    Spectrum,
    Spectra,
    read,
    get_num_threads,
    get_filesize,
)

__all__ = [
    "RuntimeArguments",
    "DataFormat",
    "DataPositions",
    "Division",
    "BaseFile",
    "MZMLFile",
    "MSZFile",
    "Spectrum",
    "Spectra",
    "read",
    "get_num_threads",
    "get_filesize",
    "__version__",
]