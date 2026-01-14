"""A versatile compression tool for efficient management of mass-spectrometry data."""

__version__ = "1.0.7"


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
    get_num_threads,
    get_filesize,
)

from .utils import read

from .metadata import (
    # Core abstractions
    MetadataBuilder,
    FieldDefinition,
    RecordSetDefinition,
    FileDistribution,
    DataCollectionInfo,
    JoinDefinition,
    JoinStrategy,
    # Builders
    MSZMetadataBuilder,
    SearchResultsMetadataBuilder,
    PercolatorMetadataBuilder,
    PepXMLMetadataBuilder,
    CompositeMetadataBuilder,
    # Convenience functions
    build_msz_metadata,
    build_composite_metadata,
)

from .mszx import (
    # MSZX classes
    MSZXFile,
    MSZXBuilder,
    MSZXManifest,
    AnnotationEntry,
    # Convenience functions
    create_mszx,
)

from .annotations import (
    # Search results types
    PSM,
    TSVReader,
    PepXMLReader,
)

__all__ = [
    # Core types
    "RuntimeArguments",
    "DataFormat",
    "DataPositions",
    "Division",
    "BaseFile",
    "MZMLFile",
    "MSZFile",
    "Spectrum",
    "Spectra",
    "get_num_threads",
    "get_filesize",
    "__version__",
    # Utility functions
    "read",
    # Metadata abstractions
    "MetadataBuilder",
    "FieldDefinition",
    "RecordSetDefinition",
    "FileDistribution",
    "DataCollectionInfo",
    "JoinDefinition",
    "JoinStrategy",
    # Metadata builders
    "MSZMetadataBuilder",
    "SearchResultsMetadataBuilder",
    "PercolatorMetadataBuilder",
    "PepXMLMetadataBuilder",
    "CompositeMetadataBuilder",
    # Metadata convenience functions
    "build_msz_metadata",
    "build_composite_metadata",
    # MSZX types
    "MSZXFile",
    "MSZXBuilder",
    "MSZXManifest",
    "AnnotationEntry",
    # MSZX convenience functions
    "create_mszx",
    # Search results types
    "PSM",
    "TSVReader",
    "PepXMLReader",
]

