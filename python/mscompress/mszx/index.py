"""Per-spectrum index of a batch (v2) MSZX archive.

A batch archive written with the index on carries one ``index.mszxi`` member:
fixed-width columns with one row per spectrum, ordered by (entry,
spectrum_idx), plus a sorted copy of selected keys (RT and precursor m/z) for
fast range queries. The binary layout is documented in ``src/mszx_index.c``;
this module reads it with numpy views over a memory map, so nothing is parsed
or copied up front and no MSZ member is opened.

Missing values are NaN in float columns and 0 in integer ones (``charge``,
``ms_level``). Missing values never match a range.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator
from typing import TYPE_CHECKING, Any, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np
import numpy.typing as npt

if TYPE_CHECKING:
    import pandas as pd

    from mscompress._core import Spectrum  # ty: ignore[unresolved-import]
    from mscompress.mszx.batch import MSZXBatchFile

__all__ = ["QueryResult", "SpectrumIndex"]

_MAGIC = b"MSZI"
_FORMAT_VERSION = 1
_HEADER = struct.Struct("<4sHHQIIII")  # 32 bytes
_COLUMN_DIR = struct.Struct("<IIQQ")  # 24 bytes
_KEY_DIR = struct.Struct("<IIQQQ")  # 32 bytes

# Column ids and dtype codes are fixed by the on-disk format.
_COLUMN_NAMES = {
    1: "entry",
    2: "spectrum_idx",
    3: "scan",
    4: "ms_level",
    5: "rt_sec",
    6: "precursor_mz",
    7: "charge",
    8: "tic",
    9: "base_peak_mz",
}
_DTYPES = {
    1: np.dtype("u1"),
    2: np.dtype("i1"),
    3: np.dtype("<u2"),
    4: np.dtype("<i2"),
    5: np.dtype("<u4"),
    6: np.dtype("<i4"),
    7: np.dtype("<u8"),
    8: np.dtype("<i8"),
    9: np.dtype("<f4"),
    10: np.dtype("<f8"),
}
# Short names accepted by query(), matching the CLI's --query.
_ALIASES = {
    "rt": "rt_sec",
    "mz": "precursor_mz",
    "precursor": "precursor_mz",
    "level": "ms_level",
}

Bound = Optional[float]
Term = Union[float, int, str, Tuple[Bound, Bound], Sequence[Bound]]


def _column_name(name: str) -> str:
    return _ALIASES.get(name, name)


class SpectrumIndex:
    """Read-only view over a serialized ``index.mszxi``.

    Args:
        buffer: The member's bytes; any object supporting the buffer protocol
            (``bytes``, ``memoryview``, ``np.memmap``). It is viewed, not
            copied, and must stay alive as long as this index.

    Raises:
        ValueError: if the buffer is not a valid index of a supported version.
    """

    def __init__(self, buffer: Any) -> None:
        data = np.frombuffer(buffer, dtype=np.uint8)
        if len(data) < _HEADER.size:
            raise ValueError("Spectrum index is truncated")
        magic, version, _flags, n_rows, n_entries, n_cols, n_keys, _ = (
            _HEADER.unpack_from(data, 0)
        )
        if magic != _MAGIC:
            raise ValueError("Not a spectrum index (bad magic)")
        if version != _FORMAT_VERSION:
            raise ValueError(f"Unsupported spectrum index version {version}")

        self._data = data
        self._n_rows = int(n_rows)
        self._n_entries = int(n_entries)
        self._columns: Dict[str, np.ndarray] = {}
        # name -> (perm, keys of valid rows, keys as float64 once needed)
        self._keys: Dict[str, List[Any]] = {}

        off = _HEADER.size
        for _ in range(n_cols):
            col_id, dtype, col_off, nbytes = _COLUMN_DIR.unpack_from(data, off)
            off += _COLUMN_DIR.size
            name = _COLUMN_NAMES.get(col_id)
            dt = _DTYPES.get(dtype)
            if name is None or dt is None:
                continue  # unknown column: skip
            self._columns[name] = self._view(col_off, nbytes, dt, name)

        for _ in range(n_keys):
            col_id, dtype, n_valid, perm_off, keys_off = _KEY_DIR.unpack_from(
                data, off
            )
            off += _KEY_DIR.size
            name = _COLUMN_NAMES.get(col_id)
            dt = _DTYPES.get(dtype)
            if name is None or dt is None:
                continue
            if n_valid > self._n_rows:
                raise ValueError(f"Corrupt sorted key for {name!r}")
            perm = self._view(perm_off, self._n_rows * 4, np.dtype("<u4"), name)
            keys = self._view(keys_off, self._n_rows * dt.itemsize, dt, name)
            if self._n_rows and int(perm.max()) >= self._n_rows:
                raise ValueError(f"Corrupt permutation for {name!r}")
            self._keys[name] = [perm, keys[: int(n_valid)], None]

    def _view(self, offset: int, nbytes: int, dtype: np.dtype, name: str) -> np.ndarray:
        if nbytes != self._n_rows * dtype.itemsize or offset + nbytes > len(self._data):
            raise ValueError(f"Corrupt spectrum index section for {name!r}")
        return self._data[offset : offset + nbytes].view(dtype)

    # -- metadata ------------------------------------------------------------

    def __len__(self) -> int:
        return self._n_rows

    @property
    def n_entries(self) -> int:
        """Number of MSZ members the index covers."""
        return self._n_entries

    @property
    def columns(self) -> List[str]:
        """Column names present, in storage order."""
        return list(self._columns)

    @property
    def sorted_by(self) -> List[str]:
        """Columns with a sorted key."""
        return list(self._keys)

    def column(self, name: str) -> np.ndarray:
        """One column as a read-only numpy array (one value per row)."""
        try:
            return self._columns[_column_name(name)]
        except KeyError:
            raise KeyError(f"No column {name!r} in the spectrum index") from None

    # -- queries -------------------------------------------------------------

    def select(self, **terms: Term) -> npt.NDArray[np.uint32]:
        """Rows matching every term, ascending (so grouped by entry).

        Each term is a column name (or alias: ``rt``, ``mz``, ``precursor``,
        ``level``) mapped to either a value (equality) or an inclusive
        ``(lo, hi)`` range, where ``None`` leaves that side open. No terms
        selects every row.
        """
        bounds = [(_column_name(k), *_bounds(k, v)) for k, v in terms.items()]
        for name, _, _ in bounds:
            if name not in self._columns:
                raise KeyError(f"No column {name!r} in the spectrum index")

        # Drive the scan from the most selective sorted key, if any.
        driver = None
        for i, (name, lo, hi) in enumerate(bounds):
            if name not in self._keys:
                continue
            first, last = self._key_bounds(name, lo, hi)
            if driver is None or last - first < driver[2] - driver[1]:
                driver = (i, first, last)

        if driver is None:
            rows = np.arange(self._n_rows, dtype=np.uint32)
        else:
            i, first, last = driver
            rows = np.sort(self._keys[bounds[i][0]][0][first:last])

        for j, (name, lo, hi) in enumerate(bounds):
            if driver is not None and j == driver[0]:
                continue
            values = self._columns[name][rows].astype(np.float64)
            rows = rows[(values >= lo) & (values <= hi)]
        return rows

    def _key_bounds(self, name: str, lo: float, hi: float) -> Tuple[int, int]:
        entry = self._keys[name]
        if entry[2] is None:
            # Compare in float64 so float32 keys match the C reader exactly.
            entry[2] = entry[1].astype(np.float64)
        keys = entry[2]
        first = int(np.searchsorted(keys, lo, side="left"))
        last = int(np.searchsorted(keys, hi, side="right"))
        return first, max(first, last)

    def to_numpy(self, rows: Optional[npt.ArrayLike] = None) -> np.ndarray:
        """Rows (all by default) as a structured array, one field per column."""
        idx = np.arange(self._n_rows) if rows is None else np.asarray(rows)
        out = np.empty(
            len(idx), dtype=[(n, c.dtype.newbyteorder("=")) for n, c in self._columns.items()]
        )
        for name, col in self._columns.items():
            out[name] = col[idx]
        return out


def _bounds(name: str, value: Term) -> Tuple[float, float]:
    if isinstance(value, (tuple, list)):
        if len(value) != 2:
            raise ValueError(f"{name}: a range must be (lo, hi)")
        lo, hi = value
        return (
            -np.inf if lo is None else float(lo),
            np.inf if hi is None else float(hi),
        )
    if isinstance(value, str):
        raise TypeError(f"{name}: expected a number or a (lo, hi) range")
    v = float(value)
    return v, v


class QueryResult:
    """Spectra of a batch archive matching a :meth:`MSZXBatchFile.query`.

    Holds only row numbers into the archive's spectrum index; metadata comes
    from the index and no MSZ member is opened until :meth:`spectra` is
    called.
    """

    def __init__(
        self,
        archive: MSZXBatchFile,
        index: SpectrumIndex,
        rows: npt.NDArray[np.uint32],
    ) -> None:
        self._archive = archive
        self._index = index
        self._rows = rows

    @property
    def rows(self) -> npt.NDArray[np.uint32]:
        """Matching row numbers into the spectrum index, ascending."""
        return self._rows

    def __len__(self) -> int:
        return len(self._rows)

    def __repr__(self) -> str:
        return f"<QueryResult {len(self)} spectra>"

    def to_numpy(self) -> np.ndarray:
        """Matching rows as a structured array, one field per index column."""
        return self._index.to_numpy(self._rows)

    def to_frame(self) -> pd.DataFrame:
        """Matching rows as a DataFrame, with an ``entry_name`` column first."""
        import pandas as pd

        frame = pd.DataFrame(self.to_numpy())
        names = np.asarray(self._archive.names, dtype=object)
        frame.insert(0, "entry_name", names[frame["entry"].to_numpy()])
        return frame

    def spectra(self) -> Iterator[Spectrum]:
        """The matching spectra, in row order.

        Hits are grouped by entry, so each member MSZ is opened once (and kept
        open by the archive), and reads within it stay local.
        """
        entries = self._index.column("entry")[self._rows]
        positions = self._index.column("spectrum_idx")[self._rows]
        current = None
        member = None
        for entry, position in zip(entries.tolist(), positions.tolist()):
            if entry != current:
                member = self._archive.get(entry)
                current = entry
            yield member.spectra[position]
