"""The PSM table: one row per peptide-spectrum match."""

from __future__ import annotations

from os import PathLike
from typing import Any, Dict, Iterable, List, Mapping, Optional, Union

import numpy as np
import pandas as pd

from mscompress.annotations.metadata import SearchMetadata

# Columns every PSMTable has, in this order. Readers add their own columns
# after these (engine scores, rank, ...); those are stored in mzTab as
# ``opt_global_<name>``.
PSM_COLUMNS: List[str] = [
    "scan",            # scan number the PSM was identified in
    "peptide",         # bare amino-acid sequence
    "modifications",   # mzTab notation, e.g. "1-UNIMOD:35,5-UNIMOD:4"
    "charge",
    "score",           # primary score, named by metadata.primary_score
    "q_value",
    "pep",             # posterior error probability
    "retention_time",  # seconds
    "precursor_mz",    # observed
    "calc_mz",         # theoretical
    "proteins",        # list of protein accessions
    "pre",             # residue before the peptide
    "post",            # residue after the peptide
    "is_decoy",
]

_INT_COLUMNS = ("scan", "charge")
_FLOAT_COLUMNS = ("score", "q_value", "pep", "retention_time", "precursor_mz", "calc_mz")


def _normalize(df: pd.DataFrame) -> pd.DataFrame:
    """Add missing core columns, put them first and fix their dtypes."""
    df = df.copy()
    n = len(df)
    for col in PSM_COLUMNS:
        if col not in df.columns:
            df[col] = None
    for col in _INT_COLUMNS:
        df[col] = pd.to_numeric(df[col], errors="coerce").astype("Int64")
    for col in _FLOAT_COLUMNS:
        df[col] = pd.to_numeric(df[col], errors="coerce").astype("float64")
    df["is_decoy"] = df["is_decoy"].fillna(False).astype(bool)
    df["proteins"] = pd.Series(
        [list(p) if isinstance(p, (list, tuple, np.ndarray)) else [] for p in df["proteins"]],
        index=df.index, dtype=object,
    )
    for col in ("peptide", "modifications", "pre", "post"):
        df[col] = df[col].astype(object).where(df[col].notna(), None)
    extras = [c for c in df.columns if c not in PSM_COLUMNS]
    df = df[PSM_COLUMNS + extras]
    df.index = pd.RangeIndex(n)
    return df


class PSMTable:
    """
    Peptide-spectrum matches as a pandas DataFrame, plus the metadata of the
    search that produced them.

    Every reader returns a ``PSMTable``, and an MSZX archive stores it as
    mzTab. ``df`` always has the columns in :data:`PSM_COLUMNS` (missing
    values are NA); readers add engine-specific columns after them.

    Example:
        ```python
        table = read_psms("results.pepXML")
        table.df[table.df.q_value < 0.01]
        table.for_scan(1234)
        ```
    """

    def __init__(
        self,
        df: Union[pd.DataFrame, Iterable[Mapping[str, Any]], None] = None,
        metadata: Optional[SearchMetadata] = None,
    ):
        """
        Args:
            df: PSMs as a DataFrame or an iterable of row dicts. It is copied;
                treat ``self.df`` as read-only (build a new table to change it).
            metadata: Search metadata.
        """
        if not isinstance(df, pd.DataFrame):
            df = pd.DataFrame(list(df) if df is not None else [])
        self.df: pd.DataFrame = _normalize(df)
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
        return PSMTable(self.df[keep], self.metadata)

    def to_mztab(self, path: Union[str, PathLike, None] = None) -> str:
        """
        Serialize as mzTab 1.0. See :func:`mscompress.annotations.write_mztab`.
        """
        from mscompress.annotations.mztab import write_mztab

        return write_mztab(self, path)
