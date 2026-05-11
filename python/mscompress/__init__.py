"""A versatile compression tool for efficient management of mass-spectrometry data."""

__version__ = "1.0.15"


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
    list_algorithms,
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

from .types import (
    AlgorithmInfo,
    SpectrumDict,
    SpectrumTransform,
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

# Optional parquet support (requires the [parquet] extra).
try:
    from .parquet import (
        parquet_to_msz,
        parquet_to_annotations_tsv,
        parquet_to_mszx,
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
        "parquet_to_msz",
        "parquet_to_annotations_tsv",
        "parquet_to_mszx",
        "to_parquet",
    ])

