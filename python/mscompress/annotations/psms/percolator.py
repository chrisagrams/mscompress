"""
Percolator file readers.

This module provides readers for Percolator input (.pin) files.
"""

from __future__ import annotations

import csv
import re
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Union

from mscompress.annotations._base import BaseAnnotationFile
from mscompress.annotations.psms._base import BasePSMReader
from mscompress.annotations.psms._types import PSM
from mscompress.types import AnnotationFormat


class TSVReader(BasePSMReader):
    """
    Reader for Percolator TSV/PIN files.

    Supports reading from file paths (compressed or uncompressed), 
    tar archive members, or raw bytes via BaseAnnotationFile.

    Example:
        >>> reader = TSVReader("results.pin")
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
    # Also handles _rank suffix seen in some outputs (e.g. _1)
    SPECID_PATTERN = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)(?:_(\d+))?")
    # Regex for format PSMId: file.scan.scan.charge_rank
    PSMID_PATTERN = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)_(\d+)")

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
        return AnnotationFormat.PERCOLATOR_TSV

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
            delimiter = "\t"
        elif header_line.startswith("PSMId"):
            headers = header_line.split("\t")
            delimiter = "\t"
        else:
            # Try to detect delimiter
            try:
                dialect = csv.Sniffer().sniff(text[:4096])
                headers = header_line.split(dialect.delimiter)
                delimiter = dialect.delimiter
            except csv.Error:
                # Fallback to tab
                headers = header_line.split("\t")
                delimiter = "\t"

        # Create reader for remaining lines
        reader = csv.DictReader(lines[1:], fieldnames=headers, delimiter=delimiter)

        for row in reader:
            psm = self._parse_row(row, headers)
            if psm:
                self._psms.append(psm)

    def _parse_row(self, row: Dict[str, str], headers: List[str]) -> Optional[PSM]:
        """Parse a single row into a PSM."""
        try:
            # Parse SpecId for scan and charge
            # Check for standard PIN "SpecId" first, then Percolator TSV "PSMId"
            spec_id = row.get("SpecId", row.get("PSMId", ""))
            
            scan_number = 0
            charge = 0
            
            # Try standard pattern first
            match = self.SPECID_PATTERN.match(spec_id)
            if match:
                scan_number = int(match.group(1))
                charge = int(match.group(3))
            else:
                # Fallback to ScanNr column if present
                if "ScanNr" in row:
                    scan_number = int(row.get("ScanNr", 0))
                
                # Fallback to Charge column if present
                if "Charge" in row:
                    charge = int(row["Charge"])
                elif "charge" in row:
                    charge = int(row["charge"])
                else:
                    charge = 2 # Default fallback

            # Get peptide
            # Standard PIN: Peptide, TSV: peptide
            peptide = row.get("Peptide", row.get("peptide", ""))
            
            # Clean up peptide format (remove flanking residues like K.PEPTIDE.R)
            if "." in peptide and len(peptide.split(".")) >= 3:
                parts = peptide.split(".")
                # Check if flanking residues are single letters (standard format)
                if len(parts[0]) <= 1 and len(parts[-1]) <= 1:
                     peptide = parts[1]
                # If it matches X.PEPTIDE.X format, take middle. 
                # Note: Some formats might be deeper, but this is standard.

            # Get label (1 = target, -1 = decoy)
            # Standard PIN: Label
            label_str = row.get("Label")
            if label_str is not None:
                label = int(label_str)
                is_decoy = label == -1
            else:
                is_decoy = False

            # Get proteins
            # Standard PIN: Proteins, TSV: proteinIds
            proteins_str = row.get("Proteins", row.get("proteinIds", ""))
            if ";" in proteins_str:
                proteins = [p.strip() for p in proteins_str.split(";") if p.strip()]
            else:
                proteins = [p.strip() for p in proteins_str.split(",") if p.strip()]

            # If no Label column, check decoy prefix
            if label_str is None:
                 is_decoy = any(self._decoy_prefix in str(v) for v in row.values())
                 # Also check protein names
                 if not is_decoy and proteins:
                     is_decoy = any(self._decoy_prefix in p for p in proteins)

            # Collect feature scores
            score = 0.0
            extra: Dict[str, Any] = {}
            
            # Columns to exclude from extra
            exclude_cols = {"SpecId", "PSMId", "Label", "ScanNr", "Peptide", "peptide", "Proteins", "proteinIds"}

            for h in headers:
                if h in exclude_cols:
                    continue
                val = row.get(h, "")
                try:
                    num_val = float(val)
                    extra[h] = num_val
                    
                    # Heuristics for score
                    # Standard PIN often uses first feature.
                    # TSV has 'score' column.
                    if h.lower() == "score":
                        score = num_val
                    elif score == 0.0 and h not in ("ExpMass", "CalcMass", "deltCn", "q-value", "posterior_error_prob", "ScanNr", "Charge", "charge"):
                         # Only use first feature if we haven't found a 'score' column yet
                         # AND it's not a metadata column
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

    def filter_to_file(self, output_path: Union[str, Path], scan_numbers: Set[int]) -> None:
        """
        Write a subset of PSMs matching the given scan numbers to a new file.

        Args:
            output_path: Path to the output file.
            scan_numbers: Set of scan numbers to include.
        """
        # Get decompressed data from source
        data = self._source.read()
        text = data.decode("utf-8")
        
        lines = text.splitlines()
        if not lines:
            return
            
        with open(output_path, 'w', newline='') as f:
            # Write header
            f.write(lines[0] + '\n')
            
            # Identify scan number logic (similar to _parse_row)
            header_line = lines[0].strip()
            if header_line.startswith("SpecId"):
                headers = header_line.split("\t")
                delimiter = "\t"
            elif header_line.startswith("PSMId"):
                headers = header_line.split("\t")
                delimiter = "\t"
            else:
                try:
                    dialect = csv.Sniffer().sniff(text[:4096])
                    headers = header_line.split(dialect.delimiter)
                    delimiter = dialect.delimiter
                except csv.Error:
                     headers = header_line.split("\t")
                     delimiter = "\t"
    
            # Write matching rows            
            for line in lines[1:]:
                row = line.strip().split(delimiter)
                if len(row) != len(headers):
                    continue
                    
                row_dict = dict(zip(headers, row))
                
                # Extract scan number
                try:
                    spec_id = row_dict.get("SpecId", row_dict.get("PSMId", ""))
                    match = self.SPECID_PATTERN.match(spec_id)
                    if match:
                        scan = int(match.group(1))
                    else:
                        scan = int(row_dict.get("ScanNr", 0))
                    
                    if scan in scan_numbers:
                        f.write(line + '\n')
                except (ValueError, IndexError):
                    continue
