"""
Percolator file readers.

This module provides readers for Percolator input (.pin) files.
"""

from __future__ import annotations

import csv
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Union

from ._base import BaseSearchResultsReader
from ._types import PSM


class PINReader(BaseSearchResultsReader):
    """
    Reader for Percolator PIN (input) files.

    PIN files are tab-separated with a header row containing column names.
    Standard columns include: SpecId, Label, ScanNr, Peptide, Proteins
    Additional columns are features used by Percolator.

    Example:
        >>> reader = PINReader("results.pin")
        >>> for psm in reader:
        ...     print(psm.peptide, psm.score)
    """

    # Regex to parse SpecId format: file.scan.scan.charge
    SPECID_PATTERN = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)")

    def __init__(
        self,
        file_path: Union[str, Path],
        decoy_prefix: str = "DECOY_",
    ):
        """
        Initialize the PIN reader.

        Args:
            file_path: Path to the .pin file.
            decoy_prefix: Prefix used to identify decoy proteins.
        """
        super().__init__(file_path)
        self._decoy_prefix = decoy_prefix

    @property
    def format(self) -> str:
        """Return the format identifier."""
        return "pin"

    def _parse(self) -> None:
        """Parse the PIN file."""
        with open(self._file_path, "r", newline="") as f:
            # Read header
            header_line = f.readline().strip()
            if header_line.startswith("SpecId"):
                headers = header_line.split("\t")
            else:
                # Try to detect delimiter
                f.seek(0)
                dialect = csv.Sniffer().sniff(f.read(4096))
                f.seek(0)
                headers = f.readline().strip().split(dialect.delimiter)

            # Create reader for remaining lines
            reader = csv.DictReader(f, fieldnames=headers, delimiter="\t")

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
