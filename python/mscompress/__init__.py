"""A versatile compression tool for efficient management of mass-spectrometry data."""

__version__ = "1.0.16"  # x-release-please-version


from mscompress._core import (
    RuntimeArguments,
    DataFormat,
    DataPositions,
    Division,
    BaseFile,
    MZMLFile,
    MSZFile,
    Spectrum,
    Spectra,
    CACHE_SPECTRA_AUTO,
    get_num_threads,
    get_filesize,
    list_algorithms,
)

from mscompress.utils import read

from mscompress.metadata import (
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

from mscompress.types import (
    AlgorithmInfo,
    SpectrumDict,
    SpectrumTransform,
)

from mscompress.mszx import (
    # MSZX classes
    MSZXFile,
    MSZXBuilder,
    MSZXManifest,
    AnnotationEntry,
    # Convenience functions
    create_mszx,
)

from mscompress.annotations import (
    # Search results types
    PSM,
    TSVReader,
    PepXMLReader,
)

# Optional parquet support (requires the [parquet] extra).
try:
    from mscompress.parquet import (
        from_parquet,
        to_parquet,
    )
    _HAS_PARQUET = True
except ImportError:
    _HAS_PARQUET = False

__all__ = [
    # Core types
    "AlgorithmInfo",
    "RuntimeArguments",
    "DataFormat",
    "DataPositions",
    "Division",
    "BaseFile",
    "MZMLFile",
    "MSZFile",
    "Spectrum",
    "Spectra",
    "SpectrumDict",
    "SpectrumTransform",
    "get_num_threads",
    "get_filesize",
    "list_algorithms",
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

if _HAS_PARQUET:
    __all__.extend([
        "from_parquet",
        "to_parquet",
    ])

