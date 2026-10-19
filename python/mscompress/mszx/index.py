"""Per-spectrum index of a batch (v2) MSZX archive.

A batch archive written with the index on carries one ``index.mszxi`` member:
fixed-width columns with one row per spectrum, ordered by (entry,
spectrum_idx), plus a sorted key on RT and precursor m/z for fast range
queries. ``entry`` and ``spectrum_idx`` are not stored; they come from a table
of each entry's first row. The binary layout is documented in ``src/mszx_index.c``;
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
_KEY_DIR = struct.Struct("<IIQQ")  # 24 bytes

# Sorted-key kinds: a permutation of the non-NaN rows, or none because the
# column never decreases within an entry (searched entry by entry).
_KEY_PERM = 1
_KEY_RUNS = 2
# Not stored; derived from the entry offsets.
_DERIVED = ("entry", "spectrum_idx")

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
        # name -> perm (None for a key that never decreases within an entry)
        self._keys: Dict[str, Optional[np.ndarray]] = {}
        self._derived: Dict[str, np.ndarray] = {}

        off = _HEADER.size
        n_offsets = self._n_entries + 1
        if off + n_offsets * 8 > len(data):
            raise ValueError("Spectrum index is truncated")
        self._offsets = data[off : off + n_offsets * 8].view("<u8")
        offsets = self._offsets
        if (
            offsets[0] != 0
            or offsets[-1] != self._n_rows
            or (np.diff(offsets.astype(np.int64)) < 0).any()
        ):
            raise ValueError("Corrupt spectrum index entry offsets")
        off += n_offsets * 8

        for _ in range(n_cols):
            col_id, dtype, col_off, nbytes = _COLUMN_DIR.unpack_from(data, off)
            off += _COLUMN_DIR.size
            name = _COLUMN_NAMES.get(col_id)
            dt = _DTYPES.get(dtype)
            if name is None or name in _DERIVED or dt is None:
                continue  # unknown column: skip
            if nbytes != self._n_rows * dt.itemsize:
                raise ValueError(f"Corrupt spectrum index section for {name!r}")
            self._columns[name] = self._view(col_off, nbytes, dt, name)

        for _ in range(n_keys):
            col_id, kind, n_valid, perm_off = _KEY_DIR.unpack_from(data, off)
            off += _KEY_DIR.size
            name = _COLUMN_NAMES.get(col_id)
            if name not in self._columns:
                continue  # a key reads values from its column
            if kind == _KEY_RUNS:
                self._keys[name] = None
            elif kind == _KEY_PERM:
                if n_valid > self._n_rows:
                    raise ValueError(f"Corrupt sorted key for {name!r}")
                perm = self._view(perm_off, int(n_valid) * 4, np.dtype("<u4"), name)
                if len(perm) and int(perm.max()) >= self._n_rows:
                    raise ValueError(f"Corrupt permutation for {name!r}")
                self._keys[name] = perm

    def _view(self, offset: int, nbytes: int, dtype: np.dtype, name: str) -> np.ndarray:
        if offset % 8 or offset + nbytes > len(self._data):
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
        """Column names: ``entry``, ``spectrum_idx``, then the stored ones."""
        return [*_DERIVED, *self._columns]

    @property
    def sorted_by(self) -> List[str]:
        """Columns with a sorted key."""
        return list(self._keys)

    def column(self, name: str) -> np.ndarray:
        """One column as a read-only numpy array (one value per row)."""
        name = _column_name(name)
        if name in _DERIVED:
            if name not in self._derived:
                values = self.take(name, np.arange(self._n_rows, dtype=np.uint32))
                values.flags.writeable = False
                self._derived[name] = values
            return self._derived[name]
        try:
            return self._columns[name]
        except KeyError:
            raise KeyError(f"No column {name!r} in the spectrum index") from None

    def take(self, name: str, rows: npt.ArrayLike) -> np.ndarray:
        """Values of one column at ``rows``, without materializing the column."""
        name = _column_name(name)
        rows = np.asarray(rows)
        if name in _DERIVED:
            entry = (
                np.searchsorted(self._offsets, rows, side="right") - 1
            ).astype(np.uint32)
            if name == "entry":
                return entry
            return (rows - self._offsets[entry]).astype(np.uint32)
        return self.column(name)[rows]

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
            if name not in self._columns and name not in _DERIVED:
                raise KeyError(f"No column {name!r} in the spectrum index")

        # Drive the scan from the most selective term with a fast path.
        driver: Optional[Tuple[int, np.ndarray]] = None
        for i, (name, lo, hi) in enumerate(bounds):
            rows = self._candidates(name, lo, hi)
            if rows is not None and (driver is None or len(rows) < len(driver[1])):
                driver = (i, rows)

        if driver is None:
            rows = np.arange(self._n_rows, dtype=np.uint32)
        else:
            rows = driver[1]

        for j, (name, lo, hi) in enumerate(bounds):
            if driver is not None and j == driver[0]:
                continue
            values = self.take(name, rows).astype(np.float64)
            rows = rows[(values >= lo) & (values <= hi)]
        return rows

    def _candidates(
        self, name: str, lo: float, hi: float
    ) -> Optional[npt.NDArray[np.uint32]]:
        """Ascending rows matching one term, or None if it has no fast path."""
        offsets = self._offsets.astype(np.int64)
        if name == "entry":
            first = 0 if lo == -np.inf else max(0, int(np.ceil(lo)))
            last = self._n_entries - 1
            if hi != np.inf:
                last = min(last, int(np.floor(hi)))
            if first > last:
                return np.empty(0, dtype=np.uint32)
            return np.arange(offsets[first], offsets[last + 1], dtype=np.uint32)
        if name not in self._keys:
            return None

        column = self._columns[name]
        perm = self._keys[name]
        if perm is not None:
            first = _bisect(lambda m: float(column[perm[m]]) < lo, len(perm))
            last = _bisect(lambda m: float(column[perm[m]]) <= hi, len(perm))
            return np.sort(perm[first : max(first, last)])

        # The column never decreases within an entry: search each one.
        # Compare in float64 so float32 values match the C reader exactly.
        lo64, hi64 = np.float64(lo), np.float64(hi)
        spans = []
        for a, b in zip(offsets[:-1].tolist(), offsets[1:].tolist()):
            values = column[a:b]
            first = a + int(np.searchsorted(values, lo64, side="left"))
            last = a + int(np.searchsorted(values, hi64, side="right"))
            if last > first:
                spans.append(np.arange(first, last, dtype=np.uint32))
        return np.concatenate(spans) if spans else np.empty(0, dtype=np.uint32)

    def to_numpy(self, rows: Optional[npt.ArrayLike] = None) -> np.ndarray:
        """Rows (all by default) as a structured array, one field per column."""
        idx = np.arange(self._n_rows, dtype=np.uint32) if rows is None else np.asarray(rows)
        values = {name: self.take(name, idx) for name in self.columns}
        out = np.empty(
            len(idx), dtype=[(n, v.dtype.newbyteorder("=")) for n, v in values.items()]
        )
        for name, v in values.items():
            out[name] = v
        return out


def _bisect(less: Any, n: int) -> int:
    """First position in [0, n) where ``less`` turns false."""
    a, b = 0, n
    while a < b:
        m = (a + b) // 2
        if less(m):
            a = m + 1
        else:
            b = m
    return a


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
        entries = self._index.take("entry", self._rows)
        positions = self._index.take("spectrum_idx", self._rows)
        current = None
        member = None
        for entry, position in zip(entries.tolist(), positions.tolist()):
            if entry != current:
                member = self._archive.get(entry)
                current = entry
            yield member.spectra[position]
