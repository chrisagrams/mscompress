"""
Percolator file readers.

This module provides readers for Percolator input (.pin) files.
"""

from __future__ import annotations

import csv
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

from mscompress.annotations._base import BaseAnnotationFile
from mscompress.annotations.psms._base import BasePSMReader
from mscompress.annotations.psms._types import PSM
from mscompress.types import AnnotationFormat


class PINReader(BasePSMReader):
    """
    Reader for Percolator PIN (input) files.

    PIN files are tab-separated with a header row containing column names.
    Standard columns include: SpecId, Label, ScanNr, Peptide, Proteins
    Additional columns are features used by Percolator.
    
    Supports reading from file paths (compressed or uncompressed), 
    tar archive members, or raw bytes via BaseAnnotationFile.

    Example:
        >>> reader = PINReader("results.pin")
        >>> for psm in reader:
        ...     print(psm.peptide, psm.score)
        >>>
        >>> # Read from tar archive
        >>> import tarfile
        >>> with tarfile.open("archive.tar") as tar:
        ...     reader = PINReader.from_tar(tar, "results.pin")
        ...     for psm in reader:
        ...         print(psm.peptide)
    """

    # Regex to parse SpecId format: file.scan.scan.charge
    SPECID_PATTERN = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)")

    def __init__(
        self,
        source: Union[str, Path, BaseAnnotationFile],
        decoy_prefix: str = "DECOY_",
    ):
        """
        Initialize the PIN reader.

        Args:
            source: Source for reading - file path or AnnotationSource.
            decoy_prefix: Prefix used to identify decoy proteins.
        """
        super().__init__(source)
        self._decoy_prefix = decoy_prefix

    @property
    def format(self) -> AnnotationFormat:
        """Return the format identifier."""
        return AnnotationFormat.PIN

    def _parse(self) -> None:
        """Parse the PIN file."""
        # Get decompressed data from source
        data = self._source.read()
        text = data.decode("utf-8")
        
        # Parse as text
        lines = text.splitlines()
        if not lines:
            return
        
        # Read header
        header_line = lines[0].strip()
        if header_line.startswith("SpecId"):
            headers = header_line.split("\t")
        else:
            # Try to detect delimiter
            dialect = csv.Sniffer().sniff(text[:4096])
            headers = header_line.split(dialect.delimiter)

        # Create reader for remaining lines
        reader = csv.DictReader(lines[1:], fieldnames=headers, delimiter="\t")

        for row in reader:
            psm = self._parse_row(row, headers)
            if psm:
                self._psms.append(psm)

    def _parse_row(self, row: Dict[str, str], headers: List[str]) -> Optional[PSM]:
        """Parse a single row into a PSM."""
        try:
            # Parse SpecId for scan and charge
            spec_id = row.get("SpecId", "")
            match = self.SPECID_PATTERN.match(spec_id)
            if match:
                scan_number = int(match.group(1))
                charge = int(match.group(3))
            else:
                # Fallback to ScanNr column
                scan_number = int(row.get("ScanNr", 0))
                charge = int(row.get("Charge", row.get("charge", 2)))

            # Get peptide
            peptide = row.get("Peptide", "")
            # Clean up peptide format (remove flanking residues like K.PEPTIDE.R)
            if "." in peptide:
                parts = peptide.split(".")
                if len(parts) >= 3:
                    peptide = parts[1]

            # Get label (1 = target, -1 = decoy)
            label = int(row.get("Label", 1))
            is_decoy = label == -1

            # Get proteins
            proteins_str = row.get("Proteins", "")
            proteins = [p.strip() for p in proteins_str.split(",") if p.strip()]

            # If no proteins found, check for decoy prefix in any column
            if not proteins and not is_decoy:
                is_decoy = any(self._decoy_prefix in str(v) for v in row.values())

            # Collect feature scores - use first numeric feature as score
            score = 0.0
            extra: Dict[str, Any] = {}

            for h in headers:
                if h in ("SpecId", "Label", "ScanNr", "Peptide", "Proteins"):
                    continue
                val = row.get(h, "")
                try:
                    num_val = float(val)
                    extra[h] = num_val
                    if score == 0.0 and h not in ("ExpMass", "CalcMass", "deltCn"):
                        score = num_val
                except ValueError:
                    extra[h] = val

            return PSM(
                scan_number=scan_number,
                peptide=peptide,
                charge=charge,
                score=score,
                proteins=proteins,
                is_decoy=is_decoy,
                extra=extra,
            )

        except (ValueError, KeyError):
            # Skip malformed rows
            return None
