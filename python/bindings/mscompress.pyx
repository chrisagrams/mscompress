# cython: linetrace=True
import os 
import numpy as np
import warnings
cimport numpy as np
from typing import Union
from xml.etree.ElementTree import fromstring, Element, ParseError
from libc.stdlib cimport malloc, free
from libc.string cimport memcpy
from libc.math cimport nan
import math

np.import_array()

include "headers.pxi"

cdef class RuntimeArguments:
    cdef Arguments _arguments

    def __init__(self):
        self._arguments.threads = _get_num_threads()
        self._arguments.mz_lossy = "lossless"
        self._arguments.int_lossy = "lossless"
        self._arguments.blocksize = <long>1e+8
        self._arguments.mz_scale_factor = 1000
        self._arguments.int_scale_factor = 0
        self._arguments.target_xml_format = _ZSTD_compression_
        self._arguments.target_mz_format = _ZSTD_compression_
        self._arguments.target_inten_format = _ZSTD_compression_
        self._arguments.zstd_compression_level = 3

    cdef Arguments* get_ptr(self):
        return &self._arguments

    @staticmethod
    cdef RuntimeArguments from_ptr(Arguments* ptr):
        cdef RuntimeArguments obj = RuntimeArguments.__new__(RuntimeArguments)
        obj._arguments = ptr[0] # Dereference the pointer
        return obj

    property threads:
        def __get__(self):
            return self._arguments.threads
        def __set__(self, value):
            self._arguments.threads = value
    
    property blocksize:
        def __get__(self):
            return self._arguments.blocksize
        def __set__(self, value):
            self._arguments.blocksize = value

    property mz_scale_factor:
        def __get__(self):
            return self._arguments.mz_scale_factor
        def __set__(self, value):
            self._arguments.mz_scale_factor = value
    
    property int_scale_factor:
        def __get__(self):
            return self._arguments.int_scale_factor
        def __set__(self, value):
            self._arguments.int_scale_factor = value
        
    property target_xml_format:
        def __get__(self):
            return self._arguments.target_xml_format
        def __set__(self, value):
            self._arguments.target_xml_format = value
        
    property target_mz_format:
        def __get__(self):
            return self._arguments.target_mz_format
        def __set__(self, value):
            self._arguments.target_mz_format = value
    
    property target_inten_format:
        def __get__(self):
            return self._arguments.target_inten_format
        def __set__(self, value):
            self._arguments.target_inten_format = value
    
    property zstd_compression_level:
        def __get__(self):
            return self._arguments.zstd_compression_level
        def __set__(self, value):
            self._arguments.zstd_compression_level = value


cdef class DataBlock:
    cdef data_block_t _data_block

    def __init__(self, char* mem, size_t size, size_t max_size):
        self._data_block.mem = mem
        self._data_block.size = size
        self._data_block.max_size = max_size


cdef class DataFormat:
    cdef data_format_t _data_format

    @staticmethod
    cdef DataFormat from_ptr(data_format_t* ptr):
        cdef DataFormat obj = DataFormat.__new__(DataFormat)
        obj._data_format = ptr[0]  # Dereference the pointer
        return obj

    property source_mz_fmt:
        def __get__(self) -> int:
            return self._data_format.source_mz_fmt

    property source_inten_fmt:
        def __get__(self) -> int:
            return self._data_format.source_inten_fmt

    property source_compression:
        def __get__(self) -> int:
            return self._data_format.source_compression

    property source_total_spec:
        def __get__(self) -> int:
            return self._data_format.source_total_spec

    property target_xml_format:
        def __get__(self) -> int:
            return self._data_format.target_xml_format

    property target_mz_format:
        def __get__(self) -> int:
            return self._data_format.target_mz_format
    
    property target_inten_format:
        def __get__(self) -> int:
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


cdef class DataPositions:
    cdef data_positions_t *data_positions
    
    @staticmethod
    cdef DataPositions from_ptr(data_positions_t* ptr):
        cdef DataPositions obj = DataPositions.__new__(DataPositions)
        obj.data_positions = ptr
        return obj
    
    property start_positions:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.start_positions)
    
    property end_positions:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.end_positions)
    
    property total_spec:
        def __get__(self) -> int:
            return self.data_positions.total_spec


cdef class Division:
    cdef division_t* _division

    @staticmethod
    cdef Division from_ptr(division_t* ptr):
        cdef Division obj = Division.__new__(Division)
        obj._division = ptr
        return obj

    property spectra:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.spectra)

    property xml:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.xml)

    property mz:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.mz)

    property inten:
        def __get__(self) -> DataPositions:
            return DataPositions.from_ptr(self._division.inten)

    property size:
        def __get__(self) -> int:
            return self._division.size

    property scans:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<uint32_t[:shape[0]]>self._division.scans)

    property ms_levels:
        def __get__(self) -> np.ndarray:
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<uint16_t[:shape[0]]>self._division.ms_levels)

    property ret_times:
        def __get__(self) -> np.ndarray:
            if self._division.ret_times is NULL:
                return None
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.mz.total_spec
            return np.asarray(<float[:shape[0]]>self._division.ret_times)


def read(path: Union[str, bytes]) -> Union[MZMLFile, MSZFile]:
    """
    Opens and parses mzML or msz file.

    Parameters:
    path (Union[str, bytes]): Path to file. Can be a string or bytes.

    Returns:
    Union[MZMLFile, MSZFile]: An MZMLFile or MSZFile class object, depending on file contents.
    """
    if not isinstance(path, str) and not isinstance(path, bytes):
        raise ValueError("Path must be a string or bytes.")
    
    if isinstance(path, str):
        path = path.encode('utf-8')
    path = os.path.expanduser(path)
    path = os.path.abspath(path)

    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")
    
    if os.path.isdir(path):
        raise IsADirectoryError(f"{path} is a directory.")
    
    filesize = _get_filesize(path)
    fd = _open_input_file(path)
    mapping = _get_mapping(fd)
    filetype = _determine_filetype(mapping, filesize)
    if filetype == 1: # mzML
        ret = MZMLFile(path, filesize, fd)
    elif filetype == 2: # msz
        ret = MSZFile(path, filesize, fd)
    else:
        raise OSError(f"Error processing file {path}")
    return ret


cdef class MZMLFile(BaseFile):
    def __init__(self, bytes path, size_t filesize, int fd):
        super(MZMLFile, self).__init__(path, filesize, fd)
        self._df = _pattern_detect(<char*> self._mapping)
        self._positions = _scan_mzml(<char*> self._mapping, self._df, self.filesize, 7) # 7 = MSLEVEL|SCANNUM|RETTIME
        _set_compress_runtime_variables(self._arguments.get_ptr(), self._df)

    @staticmethod
    def _reopen(path: bytes):
        fs = _get_filesize(path)
        fd = _open_input_file(path)
        return MZMLFile(path, fs, fd)

    def _prepare_divisions(self):
        cdef long n_divisions = _determine_n_divisions(self._positions.size, self._arguments.blocksize)
        if n_divisions > self._positions.mz.total_spec:  # If we have more divisions than spectra, decrease number of divisions
            warnings.warn(
                f"n_divisions ({n_divisions}) > total_spec ({self._positions.mz.total_spec}). "
                f"Setting n_divisions to {self._positions.mz.total_spec}"
            )
        elif n_divisions >= self._arguments.threads:
            self._divisions = _create_divisions(self._positions, n_divisions)
        else:
            self._divisions = _create_divisions(self._positions, self._arguments.threads)
            # If we have more threads than divisions, increase the blocksize to max division size
            self._arguments.blocksize = _get_division_size_max(self._divisions)

    def compress(self, output: Union[str, bytes]):
        self._prepare_divisions()
        output_fd = self._prepare_output_fd(output) 
        _compress_mzml(<char*> self._mapping, self.filesize, self._arguments.get_ptr(), self._df, self._divisions, output_fd)
        _flush(output_fd)

    def get_mz_binary(self, size_t index):
        cdef char* dest = NULL
        cdef size_t out_len = 0
        cdef data_block_t* tmp = _alloc_data_block(self._arguments.blocksize)
        cdef char* mapping_ptr
        cdef size_t start, end
        cdef np.ndarray[np.float64_t, ndim=1] mz_array
        cdef double* double_ptr
        cdef float* float_ptr

        start = self._positions.mz.start_positions[index]
        end = self._positions.mz.end_positions[index]

        dest = <char*>malloc((end - start) * 2)
        if not dest:
            raise MemoryError("Failed to allocate memory for dest")

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        self._df.decode_source_compression_mz_fun(self._z, mapping_ptr, end - start, &dest, &out_len, tmp)

        dest += ZLIB_SIZE_OFFSET # Skip zlib header

        if self._df.source_mz_fmt == _64d_:
            count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
            double_ptr = <double*>dest

            if out_len > 0:
                mz_array = np.asarray(<np.float64_t[:count]>double_ptr)
            else:
                mz_array = np.array([], dtype=np.float64)
        elif self._df.source_mz_fmt == _32f_:
            count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
            float_ptr = <float*>dest

            if out_len > 0:
                mz_array = np.asarray(<np.float32_t[:count]>float_ptr)
            else:
                mz_array = np.array([], dtype=np.float32)
        else:
            raise NotImplementedError("Data format not implemented.")
     
        _dealloc_data_block(tmp)
        return mz_array

    def get_inten_binary(self, size_t index):
        cdef char* dest = NULL
        cdef size_t out_len = 0
        cdef data_block_t* tmp = _alloc_data_block(self._arguments.blocksize)
        cdef char* mapping_ptr
        cdef size_t start, end
        cdef np.ndarray[np.float64_t, ndim=1] inten_array
        cdef double* double_ptr
        cdef float* float_ptr

        start = self._positions.inten.start_positions[index]
        end = self._positions.inten.end_positions[index]

        dest = <char*>malloc((end - start) * 2)
        if not dest:
            raise MemoryError("Failed to allocate memory for dest")

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        self._df.decode_source_compression_inten_fun(self._z, mapping_ptr, end - start, &dest, &out_len, tmp)

        dest += ZLIB_SIZE_OFFSET # Skip zlib header

        if self._df.source_inten_fmt == _64d_:
            count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
            double_ptr = <double*>dest

            if out_len > 0:
                inten_array = np.asarray(<np.float64_t[:count]>double_ptr)
            else:
                inten_array = np.array([], dtype=np.float64)
        elif self._df.source_inten_fmt == _32f_:
            count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
            float_ptr = <float*>dest

            if out_len > 0:
                inten_array = np.asarray(<np.float32_t[:count]>float_ptr)
            else:
                inten_array = np.array([], dtype=np.float32)
        else:
            raise NotImplementedError("Data format not implemented.")
     
        _dealloc_data_block(tmp)
        return inten_array

    
    def get_xml(self, size_t index):
        cdef char* res
        cdef char* mapping_ptr

        start = self._positions.spectra.start_positions[index]
        end = self._positions.spectra.end_positions[index]

        size = end-start

        mapping_ptr = <char*>self._mapping
        mapping_ptr += start

        res = <char*>malloc(size + 1)

        memcpy(res, <const void*> mapping_ptr, size)

        res[size] = '\0'

        result_str = res.decode('utf-8')

        free(res)

        element = fromstring(result_str)

        return element


cdef class MSZFile(BaseFile):
    cdef footer_t* _footer
    cdef ZSTD_DCtx* _dctx
    cdef block_len_queue_t* _xml_block_lens
    cdef block_len_queue_t* _mz_binary_block_lens
    cdef block_len_queue_t* _inten_binary_block_lens

    def __init__(self, bytes path, size_t filesize, int fd):
        super(MSZFile, self).__init__(path, filesize, fd)
        self._df = _get_header_df(self._mapping)
        self._footer = _read_footer(self._mapping, self.filesize)
        self._divisions = _read_divisions(self._mapping, self._footer.divisions_t_pos, self._footer.n_divisions)
        self._positions = _flatten_divisions(self._divisions)
        self._dctx = _alloc_dctx()
        self._xml_block_lens = _read_block_len_queue(self._mapping, self._footer.xml_blk_pos, self._footer.mz_binary_blk_pos)
        self._mz_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.mz_binary_blk_pos, self._footer.inten_binary_blk_pos)
        self._inten_binary_block_lens = _read_block_len_queue(self._mapping, self._footer.inten_binary_blk_pos, self._footer.divisions_t_pos)
        _set_decompress_runtime_variables(self._df, self._footer)

    @staticmethod
    def _reopen(path: bytes):
        fs = _get_filesize(path)
        fd = _open_input_file(path)
        return MSZFile(path, fs, fd)
    
    def decompress(self, output: Union[str, bytes]):
        output_fd = self._prepare_output_fd(output)
        _decompress_msz(<char*>self._mapping, self.filesize, self._arguments.get_ptr(), output_fd)


    def get_mz_binary(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef np.ndarray[np.float64_t, ndim=1] mz_array
        cdef double* double_ptr
        cdef float* float_ptr
        
        res = _extract_spectrum_mz(<char*> self._mapping, self._dctx, self._df, self._mz_binary_block_lens, self._footer.mz_binary_pos, self._divisions, index, &out_len, FALSE)
        
        if self._df.source_mz_fmt == _64d_:
            count = int((out_len) / 8)
            double_ptr = <double*>res
            if out_len > 0:
                mz_array = np.asarray(<np.float64_t[:count]>double_ptr)
            else:
                mz_array = np.array([], dtype=np.float64)
        elif self._df.source_mz_fmt == _32f_:
            count = int((out_len) / 4)
            float_ptr = <float*>res
            if out_len > 0:
                mz_array = np.asarray(<np.float32_t[:count]>float_ptr)
            else:
                mz_array = np.array([], dtype=np.float32)
        
        return mz_array
    
    
    def get_inten_binary(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef np.ndarray[np.float64_t, ndim=1] inten_array
        cdef double* double_ptr
        cdef float* float_ptr
        
        res = _extract_spectrum_inten(<char*> self._mapping, self._dctx, self._df, self._inten_binary_block_lens, self._footer.inten_binary_pos, self._divisions, index, &out_len, FALSE)
        
        if self._df.source_inten_fmt == _64d_:
            count = int((out_len) / 8)
            double_ptr = <double*>res
            if out_len > 0:
                inten_array = np.asarray(<np.float64_t[:count]>double_ptr)
            else:
                inten_array = np.array([], dtype=np.float64)
        elif self._df.source_inten_fmt == _32f_:
            count = int((out_len) / 4)
            float_ptr = <float*>res
            if out_len > 0:
                inten_array = np.asarray(<np.float32_t[:count]>float_ptr)
            else:
                inten_array = np.array([], dtype=np.float32)
        
        return inten_array

    
    def get_xml(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef long xml_pos, mz_pos, inten_pos
        cdef int mz_fmt, inten_fmt

        xml_pos = <long>self._footer.xml_pos
        mz_pos = <long>self._footer.mz_binary_pos
        inten_pos = <long>self._footer.inten_binary_pos
        mz_fmt = <int>self._footer.mz_fmt
        inten_fmt = <int>self._footer.inten_fmt

        res = _extract_spectra(
            <char*>self._mapping, self._dctx, self._df,
            self._xml_block_lens, self._mz_binary_block_lens,
            self._inten_binary_block_lens, xml_pos, mz_pos,
            inten_pos, mz_fmt, inten_fmt, self._divisions, index, &out_len
        )

        result_str = res.decode('utf-8')

        element = fromstring(result_str)

        return element
    

cdef class BaseFile:
    """
    Parent class for MZMLFile and MSZFile classes. Provides common interfaces for both child classes.

    Properties:
    spectra:
        Returns a Spectra class iterator to represent and manage collections of spectra in both mzML and MSZ files.

    positions:
        Returns the Division class, repesenting the positions of spectra, m/z binaries, intensity binaries, and XML in a mzML or MSZ file.
    

    Methods:
    __init__(self, bytes path, size_t filesize, int fd):
        Initializes the base attributes for file classes. This includes input file mapping, runtime arguments, and zlib z_stream.
        Other attributes (_df, _positions, etc.) are expected to be implemented by child class, as implementation varies by file.
    
    _prepare_output_fd(self, path: Union[str,bytes])->:
        Prepares a output file for compression/decompression and returns an integer representing the open file descriptor.
    
    """
    cdef bytes _path
    cdef size_t filesize
    cdef int _fd
    cdef void* _mapping
    cdef data_format_t* _df 
    cdef divisions_t* _divisions
    cdef division_t* _positions
    cdef Spectra _spectra
    cdef RuntimeArguments _arguments
    cdef z_stream* _z


    def __init__(self, bytes path, size_t filesize, int fd):
        self._path = path
        self.filesize = filesize
        self._fd = fd
        self._mapping = _get_mapping(self._fd)
        self._spectra = None
        self._arguments = RuntimeArguments()
        self._z = _alloc_z_stream()


    def __enter__(self):
        self.filesize = _get_filesize(self._path)
        self._fd = _open_input_file(self._path)
        self._mapping = _get_mapping(self._fd)
        return self
    

    def __exit__(self, exc_type, exc_value, traceback):
        self._cleanup()
    
    def __reduce__(self):
        return (self.__class__._reopen, (self._path,))

    @staticmethod
    def _reopen(path: bytes):
        fs = _get_filesize(path)
        fd = _open_input_file(path)
        return BaseFile(path, fs, fd)

    def _cleanup(self):
        if self._fd is not None and self._fd > 0:
            _close_file(self._fd)

        if self._mapping != NULL: 
            _remove_mapping(self._mapping, self.filesize)

        if self.output_fd is not None and self.output_fd > 0:
            _close_file(self.output_fd)
    

    @property
    def path(self) -> bytes:
        return self._path

    @property
    def filesize(self) -> int:
        return self.filesize
 
    @property
    def format(self) -> DataFormat:
        return DataFormat.from_ptr(self._df)
    
    @property
    def spectra(self):
        if self._spectra is None:
            self._spectra = Spectra(self, DataFormat.from_ptr(self._df), Division.from_ptr(self._positions))
        return self._spectra
    
    @property
    def positions(self):
        return Division.from_ptr(self._positions)

    @property
    def arguments(self):
        return self._arguments


    def _prepare_output_fd(self, path: Union[str, bytes]) -> int:
        if isinstance(path, str):
            path = path.encode('utf-8')
        cdef int output_fd = _open_output_file(path)
        return output_fd 

    def get_mz_binary(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")
    
    def get_inten_binary(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")

    def get_xml(self, size_t index):
        raise NotImplementedError("This method should be overridden in subclasses")

    def describe(self) -> dict:
        return {
            "path": self.path,
            "filesize": self.filesize,
            "format": DataFormat.from_ptr(self._df),
            "positions": Division.from_ptr(self._positions)
        }

    def compress(self, output):
        raise NotImplementedError("Cannot compress this file type.")
    
    def decompress(self, output):
        raise NotImplementedError("Cannot decompress this file type.")


cdef class Spectra:
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
    cdef BaseFile _f
    cdef object _df
    cdef object _positions
    cdef object _cache  # Store computed spectra
    cdef int _index
    cdef size_t length

    def __init__(self, BaseFile f, DataFormat df, Division positions):
        self._f = f
        self._df = df
        self._positions = positions
        self.length = self._df.source_total_spec
        self._cache = [None] * self.length  # Initialize cache
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
    
    def __getitem__(self, size_t index):
        if index >= self.length:
            raise IndexError("Spectra index out of range")
        
        if self._cache[index] is None:
            self._cache[index] = self._compute_spectrum(index)
        
        return self._cache[index]
    
    cdef Spectrum _compute_spectrum(self, size_t index):
        if self._positions.ret_times is not None:
            retention_time = self._positions.ret_times[index]
        else:
            retention_time = nan("1")
        return Spectrum(
            index=index,
            scan=self._positions.scans[index],
            ms_level=self._positions.ms_levels[index],
            retention_time=retention_time,
            file=self._f
        )

    def __len__(self) -> int:
        return self.length


cdef class Spectrum:
    """
    A class representing a mass spectrum within a mzML or msz file.

    Attributes:
    index (int): Index of spectrum relative to the file.
    scan (int): Scan number of spectrum reported by instrument.
    size (int): Number of m/z - intensity pairs.
    ms_level (int): MS level of spectrum.
    retention_time (float): Retention time of spectrum.
    """
    cdef:
        uint64_t index
        uint32_t scan
        uint16_t ms_level
        float _retention_time
        BaseFile _file
        object _mz
        object _intensity
        object _xml

    def __init__(self, uint64_t index, uint32_t scan, uint16_t ms_level, float retention_time, BaseFile file):
        self.index = index
        self.scan = scan
        self.ms_level = ms_level
        self._retention_time = retention_time
        self._file = file
        self._mz = None
        self._intensity = None
        self._xml = None
        
    def __repr__(self):
        return f"Spectrum(index={self.index}, scan={self.scan}, ms_level={self.ms_level}, retention_time={self.retention_time})"

    property index:
        def __get__(self):
            return self.index
    
    property scan:
        def __get__(self):
            return self.scan
    
    property xml:
        def __get__(self):
            if self._xml is None:
                self._xml = self._file.get_xml(self.index)
            return self._xml

    property size: 
        def __get__(self):
            if self._mz is None:
                self._mz = self._file.get_mz_binary(self.index)
            return len(self._mz)
    
    property ms_level:
        def __get__(self):
            return self.ms_level
    
    property retention_time:
        def __get__(self):
            if math.isnan(self._retention_time): # If the ms level wasn't derived from preprocessing step, find it
                try:
                    if self._xml is None:
                        self._xml = self._file.get_xml(self.index)
                    scan = self._xml.find('scanList/scan')
                    for param in scan.findall("cvParam"):
                        if param.attrib['accession'] == 'MS:1000016':
                            return float(param.attrib['value'])
                except ParseError as e:
                    return nan("1")
            else:
                return self._retention_time

    property mz:
        def __get__(self):
            if self._mz is None:
                self._mz = self._file.get_mz_binary(self.index)
            return self._mz

    property intensity:
        def __get__(self):
            if self._intensity is None:
                self._intensity = self._file.get_inten_binary(self.index)
            return self._intensity

    property peaks:
        def __get__(self):
            return np.column_stack((self.mz, self.intensity))

def get_num_threads() -> int:
    """
    Simple function to return current amount of threads on system.

    Returns:
    int: Number of usable threads.
    """
    return _get_num_threads()

def get_filesize(path: Union[str, bytes]) -> int:
    """
    Simple function to get filesize of file.

    Parameters:
    path (Union[str, bytes]): Path to file. Can be a string or bytes.

    Returns:
    int: Size of the file in bytes.
    """
    if isinstance(path, str):
        path = path.encode('utf-8')

    if not os.path.exists(path):
        raise FileNotFoundError(f"File not found: {path}")
    
    return _get_filesize(path)
