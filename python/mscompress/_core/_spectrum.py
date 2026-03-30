import cython
import math
import numpy as np

from xml.etree.ElementTree import ParseError

from cython.cimports.libc.math import nan
from cython.cimports.mscompress._core._bindings import (
    uint64_t,
    uint32_t,
    uint16_t,
)

from cython.cimports.mscompress._core._base import BaseFile
from cython.cimports.mscompress._core._types import DataFormat, Division


@cython.cclass
class Spectra:
    """
    A class to represent and manage a collection of spectra, allowing (lazy) iteration and access by index.

    Methods:
    __init__(self, DataFormat df, Division positions):
        Initializes the Spectra object with a data format and a list of postions.

    __iter__(self):
        Resets the iteration index and returns the iterator object.

    __next__(self):
        Returns the next spectrum in the sequence during iteration, raises `StopIteration` when the end is reached.

    __getitem__(self, size_t index):
        Computes and returns the spectrum at the specified index.
        Raises `IndexError` if the index is out of range.

    __len__(self) -> int:
        Returns the total number of spectra.
    """
    # Attributes declared in _spectrum.pxd

    def __init__(self, f: BaseFile, df: DataFormat, positions: Division):
        self._f = f
        self._df = df
        self._positions = positions
        self.length = self._df.source_total_spec
        self._cache = [None] * self.length
        self._index = 0

    def __iter__(self):
        self._index = 0  # Reset index for new iteration
        return self

    def __next__(self):
        if self._index >= self.length:
            raise StopIteration

        result = self[self._index]
        self._index += 1
        return result

    def __getitem__(self, index: cython.size_t):
        if index >= self.length:
            raise IndexError("Spectra index out of range")

        if self._cache[index] is None:
            self._cache[index] = self._compute_spectrum(index)

        return self._cache[index]

    @cython.cfunc
    def _compute_spectrum(self, index: cython.size_t) -> "Spectrum":
        if self._positions.ret_times is not None:
            retention_time = self._positions.ret_times[index]
        else:
            retention_time = nan(b"1")
        return Spectrum(
            index=index,
            scan=self._positions.scans[index],
            ms_level=self._positions.ms_levels[index],
            retention_time=retention_time,
            file=self._f
        )

    def __len__(self) -> int:
        return self.length


@cython.cclass
class Spectrum:
    """
    A class representing a mass spectrum within a mzML or msz file.

    Attributes:
    index (int): Index of spectrum relative to the file.
    scan (int): Scan number of spectrum reported by instrument.
    size (int): Number of m/z - intensity pairs.
    ms_level (int): MS level of spectrum.
    retention_time (float): Retention time of spectrum.
    """
    # Attributes declared in _spectrum.pxd

    def __init__(self, index: cython.ulonglong, scan: cython.uint, ms_level: cython.ushort, retention_time: cython.float, file: BaseFile):
        self._index = index
        self._scan = scan
        self._ms_level = ms_level
        self._retention_time = retention_time
        self._file = file
        self._mz = None
        self._intensity = None
        self._xml = None

    def __repr__(self):
        return f"Spectrum(index={self.index}, scan={self.scan}, ms_level={self.ms_level}, retention_time={self.retention_time})"

    @property
    def index(self):
        return self._index

    @property
    def scan(self):
        return self._scan

    @property
    def xml(self):
        if self._xml is None:
            self._xml = self._file.get_xml(self._index)
        return self._xml

    @property
    def size(self):
        if self._mz is None:
            self._mz = self._file.get_mz_binary(self._index)
        return len(self._mz)

    @property
    def ms_level(self):
        return self._ms_level

    @property
    def retention_time(self):
        if math.isnan(self._retention_time):  # If the ms level wasn't derived from preprocessing step, find it
            try:
                if self._xml is None:
                    self._xml = self._file.get_xml(self._index)
                scan = self._xml.find('scanList/scan')
                for param in scan.findall("cvParam"):
                    if param.attrib['accession'] == 'MS:1000016':
                        if param.attrib.get('unitAccession', '') == 'UO:0000031':  # minutes
                            return float(param.attrib['value']) * 60.0
                        else:  # seconds
                            return float(param.attrib['value'])
            except ParseError:
                return nan(b"1")
        else:
            return self._retention_time

    @property
    def mz(self):
        if self._mz is None:
            self._mz = self._file.get_mz_binary(self._index)
        return self._mz

    @property
    def intensity(self):
        if self._intensity is None:
            self._intensity = self._file.get_inten_binary(self._index)
        return self._intensity

    @property
    def peaks(self):
        mz = self.mz
        intensity = self.intensity
        if len(mz) != len(intensity):
            raise ValueError(f"Mismatch in array lengths: mz has {len(mz)} elements, intensity has {len(intensity)} elements for spectrum {self._index}")
        return np.column_stack((mz, intensity))
