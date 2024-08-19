import os 
import numpy as np
cimport numpy as np
cimport bindings
from typing import Union

np.import_array()


cdef extern from "stdint.h":
    ctypedef unsigned int uint64_t
    ctypedef unsigned int uint32_t
    ctypedef unsigned int uint16_t

cdef extern from "../src/mscompress.h":
    ctypedef struct Arguments:
        int threads
        long blocksize
        float mz_scale_factor
        float int_scale_factor
        int target_xml_format
        int target_mz_format
        int target_inten_format
        int zstd_compression_level
    
    ctypedef struct data_block_t:
        char* mem
        size_t size
        size_t max_size
    
    ctypedef struct data_format_t:
        uint32_t source_mz_fmt
        uint32_t source_inten_fmt
        uint32_t source_compression
        uint32_t source_total_spec

    ctypedef struct data_positions_t:
        uint64_t* start_positions
        uint64_t* end_positions
        int total_spec
    
    ctypedef struct division_t:
        data_positions_t* spectra
        data_positions_t* xml
        data_positions_t* mz
        data_positions_t* inten

        uint64_t size

        uint32_t* scans
        uint16_t* ms_levels
        float* ret_times

    int verbose
    int _get_num_threads "get_num_threads"()
    int _open_input_file "open_input_file"(char* input_path)
    size_t _get_filesize "get_filesize"(char* path)
    void* _get_mapping "get_mapping"(int fd)
    int _determine_filetype "determine_filetype"(void* input_map, size_t input_length)

    data_format_t* _pattern_detect "pattern_detect"(char* input_map)
    data_format_t* _get_header_df "get_header_df"(void* input_map)

    division_t* _scan_mzml "scan_mzml"(char* input_map, data_format_t* df, long end, int flags)

cdef class RuntimeArguments:
    cdef Arguments _arguments

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
        def __get__(self):
            return self._data_format.source_mz_fmt

    property source_inten_fmt:
        def __get__(self):
            return self._data_format.source_inten_fmt

    property source_compression:
        def __get__(self):
            return self._data_format.source_compression

    property source_total_spec:
        def __get__(self):
            return self._data_format.source_total_spec

    def __str__(self):
        return f"Source m/z: {self._data_format.source_mz_fmt}"

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
        def __get__(self):
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.start_positions)
    
    property end_positions:
        def __get__(self):
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self.data_positions.total_spec
            return np.asarray(<uint64_t[:shape[0]]>self.data_positions.end_positions)
    
    property total_spec:
        def __get__(self):
            return self.data_positions.total_spec


cdef class Division:
    cdef division_t* _division

    @staticmethod
    cdef Division from_ptr(division_t* ptr):
        cdef Division obj = Division.__new__(Division)
        obj._division = ptr
        return obj

    property spectra:
        def __get__(self):
            return DataPositions.from_ptr(self._division.spectra)

    property xml:
        def __get__(self):
            return DataPositions.from_ptr(self._division.xml)

    property mz:
        def __get__(self):
            return DataPositions.from_ptr(self._division.mz)

    property inten:
        def __get__(self):
            return DataPositions.from_ptr(self._division.inten)

    property size:
        def __get__(self):
            return self._division.size

    property scans:
        def __get__(self):
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.size
            return np.asarray(<uint32_t[:shape[0]]>self._division.scans)

    property ms_levels:
        def __get__(self):
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.size
            return np.asarray(<uint16_t[:shape[0]]>self._division.ms_levels)

    property ret_times:
        def __get__(self):
            cdef np.npy_intp shape[1]
            shape[0] = <np.npy_intp>self._division.size
            return np.asarray(<float[:shape[0]]>self._division.ret_times)


def read(path: Union[str, bytes]) -> Union[MZMLFile, MSZFile]:
    """
    Opens and parses mzML or msz file.

    Parameters:
    path (Union[str, bytes]): Path to file. Can be a string or bytes.

    Returns:
    Union[MZMLFile, MSZFile]: An MZMLFile or MSZFile class object, depending on file contents.
    """
    if isinstance(path, str):
        path = path.encode('utf-8')
    path = os.path.expanduser(path)
    path = os.path.abspath(path)
    filesize = _get_filesize(path)
    fd = _open_input_file(path)
    mapping = _get_mapping(fd)
    filetype = _determine_filetype(mapping, filesize)
    if filetype == 1: # mzML
        ret = MZMLFile(path, filesize, fd)
    elif filetype == 2: # msz
        ret = MSZFile(path, filesize, fd)
    return ret


cdef class MZMLFile(BaseFile):
    def __init__(self, bytes path, size_t filesize, int fd):
        super(MZMLFile, self).__init__(path, filesize, fd)
        self._df = _pattern_detect(<char*> self._mapping)
        self._positions = _scan_mzml(<char*> self._mapping, self._df, self.filesize, 7) # 7 = MSLEVEL|SCANNUM|RETTIME


cdef class MSZFile(BaseFile):
    def __init__(self, bytes path, size_t filesize, int fd):
        super(MSZFile, self).__init__(path, filesize, fd)
        self._df = _get_header_df(self._mapping)


cdef class BaseFile:
    cdef bytes path
    cdef size_t filesize
    cdef int _fd
    cdef void* _mapping
    cdef data_format_t* _df
    cdef division_t* _positions
    cdef Spectra _spectra

    def __init__(self, bytes path, size_t filesize, int fd):
        self.path = path
        self.filesize = filesize
        self._fd = fd
        self._mapping = _get_mapping(self._fd)
        self._spectra = None
    
    @property
    def spectra(self):
        if self._spectra is None:
            self._spectra = Spectra(DataFormat.from_ptr(self._df), Division.from_ptr(self._positions))
        return self._spectra

    
    def describe(self):
        return {
            "path": self.path,
            "filesize": self.filesize,
            "format": DataFormat.from_ptr(self._df),
            "positions": Division.from_ptr(self._positions)
        }


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
    cdef object _df
    cdef object _positions
    cdef object _cache  # Store computed spectra
    cdef int _index
    cdef size_t length

    def __init__(self, DataFormat df, Division positions):
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
        # Example computation using the positions
        return Spectrum(
            index=index,
            scan=self._positions.scans[index],
            size=0,
            ms_level=self._positions.ms_levels[index],
            retention_time=self._positions.ret_times[index],
            mz=np.zeros(0, dtype=np.float64),
            intensity=np.zeros(0, dtype=np.float64)
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
    mz (np.ndarray[np.float64_t, ndim=1]): A numpy array of m/z values.
    intensity (np.ndarray[np.float64_t, ndim=1]): A numpy array of intensity values.
    """
    cdef:
        uint64_t index
        uint32_t scan
        uint64_t size
        uint16_t ms_level
        float retention_time
        object mz
        object intensity

    def __init__(self, uint64_t index, uint32_t scan, uint64_t size, uint16_t ms_level, float retention_time,
                 np.ndarray[np.float64_t, ndim=1] mz, np.ndarray[np.float64_t, ndim=1] intensity):
        assert mz.shape[0] == size, "mz array size must match 'size'"
        assert intensity.shape[0] == size, "intensity array size must match 'size'"
        
        self.index = index
        self.scan = scan
        self.size = size
        self.ms_level = ms_level
        self.retention_time = retention_time
        self.mz = mz
        self.intensity = intensity

    def __repr__(self):
        return f"Spectrum(index={self.index}, scan={self.scan}, size={self.size}, ms_level={self.ms_level}, retention_time={self.retention_time})"


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
    return _get_filesize(path)
