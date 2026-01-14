"""
Croissant metadata builders for mass spectrometry datasets.

This package provides an abstraction layer for building ML Croissant metadata
from various mass spectrometry data sources including MSZ files and search
results (e.g., .pin, .pepXML files).

The architecture follows a composable pattern where different metadata builders
can be combined to create unified dataset metadata for ML training pipelines.

Example:
    Simple usage with a single MSZ file:

    >>> import mscompress
    >>> from mscompress.metadata import build_msz_metadata
    >>> msz = mscompress.read("sample.msz")
    >>> metadata = build_msz_metadata(
    ...     msz,
    ...     dataset_name="My Dataset",
    ...     description="Sample proteomics data",
    ...     repo_id="username/my-dataset"
    ... )

    Composite usage with search results:

    >>> from mscompress.metadata import (
    ...     MSZMetadataBuilder,
    ...     PercolatorMetadataBuilder,
    ...     CompositeMetadataBuilder,
    ... )
    >>> msz_builder = MSZMetadataBuilder(msz)
    >>> pin_builder = PercolatorMetadataBuilder("sample.pin")
    >>> composite = CompositeMetadataBuilder(
    ...     dataset_name="Annotated Dataset",
    ...     description="Proteomics with PSM annotations",
    ...     repo_id="username/dataset"
    ... )
    >>> composite.add_builder(msz_builder, primary=True)
    >>> composite.add_builder(pin_builder)
    >>> composite.add_join(msz_builder, pin_builder, "scan_number")
    >>> metadata = composite.build()
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict, Optional, Union

# Core types
from ._types import (
    DataCollectionInfo,
    FieldDefinition,
    FileDistribution,
    JoinDefinition,
    JoinStrategy,
    RecordSetDefinition,
)

# Base class
from ._base import MetadataBuilder

# Composite builder
from ._composite import CompositeMetadataBuilder

# Concrete builders
from .msz import MSZMetadataBuilder
from .search import (
    PepXMLMetadataBuilder,
    PercolatorMetadataBuilder,
    SearchResultsMetadataBuilder,
)

if TYPE_CHECKING:
    from .._core import MSZFile


__all__ = [
    # Core types
    "FieldDefinition",
    "RecordSetDefinition",
    "FileDistribution",
    "DataCollectionInfo",
    "JoinDefinition",
    "JoinStrategy",
    # Base class
    "MetadataBuilder",
    # Composite builder
    "CompositeMetadataBuilder",
    # Concrete builders
    "MSZMetadataBuilder",
    "SearchResultsMetadataBuilder",
    "PercolatorMetadataBuilder",
    "PepXMLMetadataBuilder",
    # Convenience functions
    "build_msz_metadata",
    "build_composite_metadata",
]

def build_msz_metadata(
    msz: MSZFile,
    dataset_name: str,
    description: str,
    repo_id: str,
) -> Dict[str, Any]:
    """
    Generate Croissant metadata for an MSZ dataset.

    This is a convenience function that wraps the class-based API
    for simple use cases with a single MSZ file.

    Croissant is a high-level format for machine learning datasets that
    provides standardized metadata about dataset structure, fields, and semantics.

    Args:
        msz: MSZFile object to extract metadata from.
        dataset_name: Human-readable name for the dataset.
        description: Description of the dataset.
        repo_id: HuggingFace repository ID.

    Returns:
        Dict containing Croissant metadata following the ML Croissant standard.

    Example:
        >>> import mscompress
        >>> from mscompress.metadata import build_msz_metadata
        >>> msz = mscompress.read("sample.msz")
        >>> metadata = build_msz_metadata(
        ...     msz,
        ...     dataset_name="My Dataset",
        ...     description="Sample proteomics data",
        ...     repo_id="username/my-dataset"
        ... )
    """
    builder = MSZMetadataBuilder(msz)
    composite = CompositeMetadataBuilder(
        dataset_name=dataset_name,
        description=description,
        repo_id=repo_id,
    )
    composite.add_builder(builder, primary=True)
    return composite.build()


def build_composite_metadata(
    msz: MSZFile,
    search_results: Optional[Union[str, Path]] = None,
    search_format: str = "pin",
    dataset_name: str = "Dataset",
    description: str = "",
    repo_id: str = "",
    join_key: str = "scan_number",
) -> Dict[str, Any]:
    """
    Generate Croissant metadata for an MSZ dataset with optional search results.

    Convenience function for creating composite metadata that includes
    both spectrum data and search results (PSMs).

    Args:
        msz: MSZFile object.
        search_results: Path to search results file (.pin or .pepXML).
        search_format: Format of search results ("pin" or "pepxml").
        dataset_name: Human-readable name for the dataset.
        description: Description of the dataset.
        repo_id: HuggingFace repository ID.
        join_key: Field to join spectrum and search data on.

    Returns:
        Dict containing Croissant metadata.

    Example:
        >>> import mscompress
        >>> from mscompress.metadata import build_composite_metadata
        >>> msz = mscompress.read("sample.msz")
        >>> metadata = build_composite_metadata(
        ...     msz,
        ...     search_results="sample.pin",
        ...     search_format="pin",
        ...     dataset_name="Annotated Dataset",
        ...     description="Proteomics with PSM annotations",
        ...     repo_id="username/annotated-data"
        ... )
    """
    composite = CompositeMetadataBuilder(
        dataset_name=dataset_name,
        description=description,
        repo_id=repo_id,
    )

    # Add MSZ metadata
    msz_builder = MSZMetadataBuilder(msz)
    composite.add_builder(msz_builder, primary=True)

    # Add search results if provided
    if search_results:
        search_path = Path(search_results)

        if search_format.lower() == "pin":
            search_builder = PercolatorMetadataBuilder(search_path)
        elif search_format.lower() == "pepxml":
            search_builder = PepXMLMetadataBuilder(search_path)
        else:
            raise ValueError(f"Unknown search format: {search_format}")

        composite.add_builder(search_builder)
        composite.add_join(msz_builder, search_builder, join_key)

    return composite.build()
