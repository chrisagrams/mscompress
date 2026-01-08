"""
Composite metadata builder for combining multiple metadata sources.

This module provides the CompositeMetadataBuilder class which allows
combining metadata from multiple sources (e.g., MSZ files and search results)
into a unified Croissant dataset.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from ._base import MetadataBuilder
from ._types import DataCollectionInfo, JoinDefinition


class CompositeMetadataBuilder:
    """
    Combines multiple metadata builders into a unified Croissant dataset.

    Allows building datasets that include both spectrum data and search
    results, with defined relationships between them.

    Example:
        >>> msz = mscompress.read("sample.msz")
        >>> msz_builder = MSZMetadataBuilder(msz)
        >>> pin_builder = PercolatorMetadataBuilder("sample.pin")
        >>>
        >>> composite = CompositeMetadataBuilder(
        ...     dataset_name="Proteomics Dataset",
        ...     description="Mass spec data with search results",
        ...     repo_id="username/dataset"
        ... )
        >>> composite.add_builder(msz_builder, primary=True)
        >>> composite.add_builder(pin_builder)
        >>> composite.add_join(msz_builder, pin_builder, "scan_number")
        >>>
        >>> metadata = composite.build()
    """

    def __init__(
        self,
        dataset_name: str,
        description: str,
        repo_id: str,
        license: str = "unknown",
    ):
        """
        Initialize the composite metadata builder.

        Args:
            dataset_name: Human-readable name for the dataset.
            description: Description of the dataset.
            repo_id: HuggingFace repository ID.
            license: License identifier (e.g., "CC-BY-4.0").
        """
        self.dataset_name = dataset_name
        self.description = description
        self.repo_id = repo_id
        self.license = license

        self._builders: List[MetadataBuilder] = []
        self._primary_builder: Optional[MetadataBuilder] = None
        self._joins: List[JoinDefinition] = []

    @property
    def base_url(self) -> str:
        """Base URL for the HuggingFace dataset."""
        return f"https://huggingface.co/datasets/{self.repo_id}/resolve/main"

    def add_builder(
        self,
        builder: MetadataBuilder,
        primary: bool = False,
    ) -> CompositeMetadataBuilder:
        """
        Add a metadata builder to the composite.

        Args:
            builder: MetadataBuilder instance to add.
            primary: If True, this is the primary data source.

        Returns:
            Self for method chaining.
        """
        self._builders.append(builder)
        if primary:
            self._primary_builder = builder
        return self

    def add_join(
        self,
        left: MetadataBuilder,
        right: MetadataBuilder,
        join_key: Optional[str] = None,
        join_type: str = "left",
    ) -> CompositeMetadataBuilder:
        """
        Define a join relationship between two metadata sources.

        Args:
            left: Left side of the join.
            right: Right side of the join.
            join_key: Field to join on (auto-detected if not specified).
            join_type: Type of join (left, right, inner, outer).

        Returns:
            Self for method chaining.
        """
        # Auto-detect join key if not specified
        key = join_key or left.join_key or right.join_key
        if not key:
            raise ValueError(
                f"No join key specified and neither {left.name} nor "
                f"{right.name} has a default join key"
            )

        self._joins.append(
            JoinDefinition(
                left_record_set=left.name,
                right_record_set=right.name,
                join_key=key,
                join_type=join_type,
            )
        )
        return self

    def _build_context(self) -> Dict[str, Any]:
        """Build the JSON-LD context."""
        return {
            "@language": "en",
            "@vocab": "https://schema.org/",
            "ml": "http://mlcommons.org/croissant/",
            "sc": "https://schema.org/",
            "column": "ml:field",
            "data": {"@id": "ml:data", "@type": "@json"},
            "dataType": {"@id": "ml:dataType", "@type": "@vocab"},
            "source": "ml:source",
            "extract": "ml:extract",
            "transform": "ml:transform",
        }

    def _build_distributions(self) -> List[Dict[str, Any]]:
        """Build all file distributions."""
        return [
            builder.get_distribution(self.base_url).to_croissant()
            for builder in self._builders
        ]

    def _build_record_sets(self) -> List[Dict[str, Any]]:
        """Build all record sets."""
        return [
            builder.get_record_set().to_croissant() for builder in self._builders
        ]

    def _build_data_collection(self) -> Dict[str, Any]:
        """Build the data collection info from primary builder."""
        if self._primary_builder:
            info = self._primary_builder.get_data_collection_info()
        elif self._builders:
            info = self._builders[0].get_data_collection_info()
        else:
            info = DataCollectionInfo(
                size_description="unknown",
                data_type="mass spectrometry",
                format_description="unknown",
            )
        return info.to_croissant()

    def build(self) -> Dict[str, Any]:
        """
        Build the complete Croissant metadata dictionary.

        Returns:
            Complete Croissant metadata following the ML Croissant standard.
        """
        metadata: Dict[str, Any] = {
            "@context": self._build_context(),
            "@type": "sc:Dataset",
            "name": self.dataset_name,
            "description": self.description,
            "url": f"https://huggingface.co/datasets/{self.repo_id}",
            "license": self.license,
            "distribution": self._build_distributions(),
            "recordSet": self._build_record_sets(),
            "ml:dataCollection": self._build_data_collection(),
        }

        # Add joins if defined
        if self._joins:
            metadata["ml:joins"] = [j.to_croissant() for j in self._joins]

        return metadata
