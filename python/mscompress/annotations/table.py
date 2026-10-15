"""The PSM table: one row per peptide-spectrum match."""

from __future__ import annotations

from typing import Any, Dict, Iterable, List, Optional

import numpy as np
import pandas as pd

from mscompress.annotations.metadata import SearchMetadata

# Columns every PSMTable has, in this order, and their dtypes. Readers add
# their own columns after these (engine scores, rank, ...); those are stored
# in mzTab as ``opt_global_<name>``.
PSM_SCHEMA: Dict[str, str] = {
    "scan": "Int64",               # scan number the PSM was identified in
    "peptide": "string",           # bare amino-acid sequence
    "modifications": "string",     # mzTab notation, e.g. "1-UNIMOD:35,5-UNIMOD:4"
    "charge": "Int64",
    "score": "float64",            # primary score, named by metadata.primary_score
    "q_value": "float64",
    "pep": "float64",              # posterior error probability
    "retention_time": "float64",   # seconds
    "precursor_mz": "float64",     # observed
    "calc_mz": "float64",          # theoretical
    "proteins": "object",          # list of protein accessions
    "pre": "string",               # residue before the peptide
    "post": "string",              # residue after the peptide
    "is_decoy": "bool",
}
PSM_COLUMNS: List[str] = list(PSM_SCHEMA)


def psm_frame(data: pd.DataFrame) -> pd.DataFrame:
    """
    The DataFrame for a PSMTable, built from ``PSM_SCHEMA``.

    Every core column is present with its schema dtype: taken from ``data``
    when it has the column (cast to that dtype), else empty (NA, no
    proteins, not a decoy). ``data``'s other columns follow. Readers build
    their tables with this.
    """
    n = len(data)
    df = pd.DataFrame({col: pd.Series(index=range(n), dtype=dtype)
                       for col, dtype in PSM_SCHEMA.items()})
    df["proteins"] = [[] for _ in range(n)]
    df["is_decoy"] = False
    if len(data.columns):
        data = data.reset_index(drop=True)
        df[list(data.columns)] = data.astype(
            {col: dtype for col, dtype in PSM_SCHEMA.items() if col in data})
    return df


class PSMTable:
    """
    Peptide-spectrum matches as a pandas DataFrame, plus the metadata of the
    search that produced them.

    Every reader returns a ``PSMTable``, and an MSZX archive stores it as
    mzTab. ``df`` has the columns in :data:`PSM_SCHEMA` first, with those
    dtypes (built with :func:`psm_frame`); readers add engine-specific
    columns after them.

    Example:
        ```python
        table = read_psms("results.pepXML")
        table.df[table.df.q_value < 0.01]
        table.for_scan(1234)
        ```
    """

    def __init__(self, df: Optional[pd.DataFrame] = None,
                 metadata: Optional[SearchMetadata] = None):
        """
        Args:
            df: PSMs, as built by :func:`psm_frame`. Treat ``self.df`` as
                read-only (build a new table to change it).
            metadata: Search metadata.
        """
        self.df: pd.DataFrame = df if df is not None else psm_frame(pd.DataFrame())
        self.metadata: SearchMetadata = metadata or SearchMetadata()
        self._rows_by_scan: Optional[Dict[int, np.ndarray]] = None
        self._records: Optional[List[Dict[str, Any]]] = None

    def __len__(self) -> int:
        return len(self.df)

    def __repr__(self) -> str:
        software = self.metadata.software or "unknown software"
        return f"PSMTable({len(self)} PSMs from {software})"

    def _scan_index(self) -> Dict[int, np.ndarray]:
        if self._rows_by_scan is None:
            scans = self.df["scan"]
            valid = scans.notna().to_numpy()
            positions = np.flatnonzero(valid)
            groups = pd.Series(positions).groupby(scans[valid].to_numpy()).indices
            self._rows_by_scan = {int(s): positions[idx] for s, idx in groups.items()}
        return self._rows_by_scan

    @property
    def scans(self) -> List[int]:
        """Scan numbers with at least one PSM, ascending."""
        return sorted(self._scan_index())

    def has_scan(self, scan: int) -> bool:
        """Whether any PSM is for this scan."""
        return int(scan) in self._scan_index()

    def for_scan(self, scan: int) -> pd.DataFrame:
        """The PSMs for one scan, as a DataFrame."""
        rows = self._scan_index().get(int(scan))
        return self.df.iloc[rows if rows is not None else []]

    def records_for_scan(self, scan: int) -> List[Dict[str, Any]]:
        """The PSMs for one scan, as row dicts (fast for per-spectrum loops)."""
        rows = self._scan_index().get(int(scan))
        if rows is None:
            return []
        if self._records is None:
            self._records = self.df.to_dict("records")
        return [self._records[i] for i in rows]

    def filter_scans(self, scans: Iterable[int]) -> PSMTable:
        """A new table with only the PSMs for the given scans."""
        keep = self.df["scan"].isin(set(int(s) for s in scans))
        return PSMTable(self.df[keep].reset_index(drop=True), self.metadata)
