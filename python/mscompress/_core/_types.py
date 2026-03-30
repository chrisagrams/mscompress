import cython
import numpy as np

from cython.cimports.numpy import import_array, PyArray_SimpleNewFromData, NPY_UINT64, NPY_UINT32, NPY_UINT16, NPY_FLOAT, npy_intp
from cython.cimports.mscompress._core._bindings import (
    Arguments,
    data_block_t,
    data_format_t,
    data_positions_t,
    division_t,
    uint64_t,
    uint32_t,
    uint16_t,
    _get_num_threads,
    _ZSTD_compression_,
)

import_array()


@cython.cclass
class RuntimeArguments:
    # _arguments declared in _types.pxd

    def __init__(self):
        self._arguments.threads = _get_num_threads()
        self._arguments.mz_lossy = b"lossless"
        self._arguments.int_lossy = b"lossless"
        self._arguments.blocksize = cython.cast(cython.long, 1e+8)
        self._arguments.mz_scale_factor = 1000
        self._arguments.int_scale_factor = 0
        self._arguments.target_xml_format = _ZSTD_compression_
        self._arguments.target_mz_format = _ZSTD_compression_
        self._arguments.target_inten_format = _ZSTD_compression_
        self._arguments.zstd_compression_level = 3

    @cython.cfunc
    def get_ptr(self) -> cython.pointer(Arguments):
        return cython.address(self._arguments)

    @staticmethod
    @cython.cfunc
    def from_ptr(ptr: cython.pointer(Arguments)) -> "RuntimeArguments":
        obj: RuntimeArguments = RuntimeArguments.__new__(RuntimeArguments)
        obj._arguments = ptr[0]
        return obj

    @property
    def threads(self) -> cython.int:
        return self._arguments.threads

    @threads.setter
    def threads(self, value):
        self._arguments.threads = value

    @property
    def blocksize(self) -> cython.long:
        return self._arguments.blocksize

    @blocksize.setter
    def blocksize(self, value):
        self._arguments.blocksize = value

    @property
    def mz_scale_factor(self) -> cython.float:
        return self._arguments.mz_scale_factor

    @mz_scale_factor.setter
    def mz_scale_factor(self, value):
        self._arguments.mz_scale_factor = value

    @property
    def int_scale_factor(self) -> cython.float:
        return self._arguments.int_scale_factor

    @int_scale_factor.setter
    def int_scale_factor(self, value):
        self._arguments.int_scale_factor = value

    @property
    def target_xml_format(self) -> cython.int:
        return self._arguments.target_xml_format

    @target_xml_format.setter
    def target_xml_format(self, value):
        self._arguments.target_xml_format = value

    @property
    def target_mz_format(self) -> cython.int:
        return self._arguments.target_mz_format

    @target_mz_format.setter
    def target_mz_format(self, value):
        self._arguments.target_mz_format = value

    @property
    def target_inten_format(self) -> cython.int:
        return self._arguments.target_inten_format

    @target_inten_format.setter
    def target_inten_format(self, value):
        self._arguments.target_inten_format = value

    @property
    def zstd_compression_level(self) -> cython.int:
        return self._arguments.zstd_compression_level

    @zstd_compression_level.setter
    def zstd_compression_level(self, value):
        self._arguments.zstd_compression_level = value


@cython.cclass
class DataBlock:
    # _data_block declared in _types.pxd

    def __init__(self, mem: cython.p_char, size: cython.size_t, max_size: cython.size_t):
        self._data_block.mem = mem
        self._data_block.size = size
        self._data_block.max_size = max_size


@cython.cclass
class DataFormat:
    # _data_format declared in _types.pxd

    @staticmethod
    @cython.cfunc
    def from_ptr(ptr: cython.pointer(data_format_t)) -> "DataFormat":
        obj: DataFormat = DataFormat.__new__(DataFormat)
        obj._data_format = ptr[0]
        return obj

    @property
    def source_mz_fmt(self) -> int:
        return self._data_format.source_mz_fmt

    @property
    def source_inten_fmt(self) -> int:
        return self._data_format.source_inten_fmt

    @property
    def source_compression(self) -> int:
        return self._data_format.source_compression

    @property
    def source_total_spec(self) -> int:
        return self._data_format.source_total_spec

    @property
    def target_xml_format(self) -> int:
        return self._data_format.target_xml_format

    @property
    def target_mz_format(self) -> int:
        return self._data_format.target_mz_format

    @property
    def target_inten_format(self) -> int:
        return self._data_format.target_inten_format

    def __str__(self):
        return f"DataFormat(source_mz_fmt={self.source_mz_fmt}, source_inten_fmt={self.source_inten_fmt}, source_compression={self.source_compression}, source_total_spec={self.source_total_spec})"

    def to_dict(self):
        return {
            'source_mz_fmt': 'MS:' + str(self._data_format.source_mz_fmt),
            'source_inten_fmt': 'MS:' + str(self._data_format.source_inten_fmt),
            'source_compression': 'MS:' + str(self._data_format.source_compression),
            'source_total_spec': self._data_format.source_total_spec
        }


@cython.cclass
class DataPositions:
    # data_positions declared in _types.pxd

    @staticmethod
    @cython.cfunc
    def from_ptr(ptr: cython.pointer(data_positions_t)) -> "DataPositions":
        obj: DataPositions = DataPositions.__new__(DataPositions)
        obj.data_positions = ptr
        return obj

    @property
    def start_positions(self) -> np.ndarray:
        shape: npy_intp[1]
        shape[0] = cython.cast(npy_intp, self.data_positions.total_spec)
        return PyArray_SimpleNewFromData(
            1, cython.cast(cython.pointer(npy_intp), cython.address(shape[0])),
            NPY_UINT64,
            cython.cast(cython.pointer(cython.void), self.data_positions.start_positions)
        )

    @property
    def end_positions(self) -> np.ndarray:
        shape: npy_intp[1]
        shape[0] = cython.cast(npy_intp, self.data_positions.total_spec)
        return PyArray_SimpleNewFromData(
            1, cython.cast(cython.pointer(npy_intp), cython.address(shape[0])),
            NPY_UINT64,
            cython.cast(cython.pointer(cython.void), self.data_positions.end_positions)
        )

    @property
    def total_spec(self) -> int:
        return self.data_positions.total_spec


@cython.cclass
class Division:
    # _division declared in _types.pxd

    @staticmethod
    @cython.cfunc
    def from_ptr(ptr: cython.pointer(division_t)) -> "Division":
        obj: Division = Division.__new__(Division)
        obj._division = ptr
        return obj

    @property
    def spectra(self) -> DataPositions:
        return DataPositions.from_ptr(self._division.spectra)

    @property
    def xml(self) -> DataPositions:
        return DataPositions.from_ptr(self._division.xml)

    @property
    def mz(self) -> DataPositions:
        return DataPositions.from_ptr(self._division.mz)

    @property
    def inten(self) -> DataPositions:
        return DataPositions.from_ptr(self._division.inten)

    @property
    def size(self) -> int:
        return self._division.size

    @property
    def scans(self) -> np.ndarray:
        shape: npy_intp[1]
        shape[0] = cython.cast(npy_intp, self._division.mz.total_spec)
        return PyArray_SimpleNewFromData(
            1, cython.cast(cython.pointer(npy_intp), cython.address(shape[0])),
            NPY_UINT32,
            cython.cast(cython.pointer(cython.void), self._division.scans)
        )

    @property
    def ms_levels(self) -> np.ndarray:
        shape: npy_intp[1]
        shape[0] = cython.cast(npy_intp, self._division.mz.total_spec)
        return PyArray_SimpleNewFromData(
            1, cython.cast(cython.pointer(npy_intp), cython.address(shape[0])),
            NPY_UINT16,
            cython.cast(cython.pointer(cython.void), self._division.ms_levels)
        )

    @property
    def ret_times(self):
        if self._division.ret_times is cython.NULL:
            return None
        shape: npy_intp[1]
        shape[0] = cython.cast(npy_intp, self._division.mz.total_spec)
        return PyArray_SimpleNewFromData(
            1, cython.cast(cython.pointer(npy_intp), cython.address(shape[0])),
            NPY_FLOAT,
            cython.cast(cython.pointer(cython.void), self._division.ret_times)
        )
