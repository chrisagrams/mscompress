# cython: linetrace=True
# cython: binding=True

import os 
import numpy as np
import warnings
import tempfile
import ctypes
cimport numpy as np
from typing import Union
from os import PathLike
from pathlib import Path
from xml.etree.ElementTree import fromstring, Element, ParseError
from libc.stdlib cimport malloc, free
from libc.string cimport memcpy, const_char
from libc.math cimport nan
import math
import re

np.import_array()

# Create a numpy dtype that matches C long (32-bit on Windows, typically 64-bit on Linux/Mac)
_c_long_dtype = np.dtype(np.int32 if ctypes.sizeof(ctypes.c_long) == 4 else np.int64)
include "_headers.pxi"

# Global error/warning handler for Python
def _install_mscompress_warning_formatter():
    import warnings
    def _mscompress_formatwarning(message, category, filename, lineno, line=None):
        return f"mscompress : {category.__name__}: {message}\n"
    warnings.formatwarning = _mscompress_formatwarning

_install_mscompress_warning_formatter()
cdef void _python_error_handler(const char* message) noexcept:
    """Callback function to handle C errors in Python"""
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    warnings.warn(msg.strip(), RuntimeWarning, stacklevel=2)

cdef void _python_warning_handler(const char* message) noexcept:
    """Callback function to handle C warnings in Python"""
    msg = message.decode('utf-8') if isinstance(message, bytes) else message
    warnings.warn(msg.strip(), RuntimeWarning, stacklevel=2)

# Initialize callbacks when module is imported
_set_error_callback(_python_error_handler)
_set_warning_callback(_python_warning_handler)

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


cdef class MZMLFile(BaseFile):
    def __init__(self, bytes path):
        super(MZMLFile, self).__init__(path)
        if self._mapping is NULL:
             raise RuntimeError("File mapping is NULL. Filesize might be 0.")
        self._df = _pattern_detect(<char*> self._mapping)
        if self._df is NULL:
             raise RuntimeError("pattern_detect returned NULL. Failed to detect mzML pattern.")
        
        self._positions = _scan_mzml(<char*> self._mapping, self._df, self.filesize, 7) # 7 = MSLEVEL|SCANNUM|RETTIME
        if self._positions is NULL:
             raise RuntimeError("scan_mzml returned NULL. The file might be empty or invalid.")
        _set_compress_runtime_variables(self._arguments.get_ptr(), self._df)

    @staticmethod
    def _reopen(path: bytes):
        return MZMLFile(path)

    def _prepare_divisions(self):
        cdef long n_divisions = _determine_n_divisions(self._positions.size, self._arguments.blocksize)
        if n_divisions > self._positions.mz.total_spec:  # If we have more divisions than spectra, decrease number of divisions
            warnings.warn(
                f"n_divisions ({n_divisions}) > total_spec ({self._positions.mz.total_spec}). "
                f"Setting n_divisions to {self._positions.mz.total_spec}"
            )
            n_divisions = self._positions.mz.total_spec
            if n_divisions == 0:
                n_divisions = 1
            self._divisions = _create_divisions(self._positions, n_divisions)
        elif n_divisions >= self._arguments.threads:
            self._divisions = _create_divisions(self._positions, n_divisions)
        else:
            self._divisions = _create_divisions(self._positions, self._arguments.threads)
            # If we have more threads than divisions, increase the blocksize to max division size
            self._arguments.blocksize = _get_division_size_max(self._divisions)

    def compress(self, output: Union[str, PathLike]) -> MSZFile:
        output = os.fspath(output)
        self._prepare_divisions()
        self.output_fd = self._prepare_output_fd(output)
        _compress_mzml(<char*> self._mapping, self.filesize, self._arguments.get_ptr(), self._df, self._divisions, self.output_fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        return MSZFile(output.encode('utf-8'))

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ) -> Union[MZMLFile, MSZFile]:
        """
        Extract spectra from an mzML file to mzML format with optional filtering.
        
        Args:
            output: Path to the output file. Must have .mzml extension (or .msz).
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
        
        Raises:
            ValueError: If output file extension is not supported.
        """
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef uint16_t c_ms_level = 0
        cdef np.ndarray indicies_arr
        cdef np.ndarray[np.uint32_t, ndim=1] scans_arr

        output = Path(output).resolve()
        output_ext = output.suffix.lower()

        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a temporary mzML file path, extract to it, then compress to MSZ
            temp_mzml_path = Path(tempfile.gettempdir()) / f"{output.stem}_temp.mzML"
            
            # Delete output file if it already exists
            if output.exists():
                output.unlink()
            
            # Extract to temporary mzML file
            self.extract(temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)
            
            # Compress the temporary mzML to MSZ
            temp_mzml = None
            try:
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                if temp_mzml is not None:
                    temp_mzml._cleanup()
                if temp_mzml_path.exists():
                    temp_mzml_path.unlink()

        elif output_ext == '.mzml':
             # Convert Python lists to C arrays
            # Use _c_long_dtype to match C long type (32-bit on Windows, 64-bit on Linux)
            if indicies is not None:
                indicies_arr = np.array(indicies, dtype=_c_long_dtype)
                c_indicies = <long*>indicies_arr.data
                indicies_length = len(indicies)
            
            if scan_numbers is not None:
                scans_arr = np.array(scan_numbers, dtype=np.uint32)
                c_scans = <uint32_t*>scans_arr.data
                scans_length = len(scan_numbers)
            
            if ms_level is not None:
                c_ms_level = <uint16_t>ms_level

            # Prepare output file
            self.output_fd = self._prepare_output_fd(str(output))

            _extract_mzml_filtered(
                <char*>self._mapping,
                self.filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                self._positions, 
                self.output_fd
            )

            # Flush and close output
            _flush(self.output_fd)
            _close_file(self.output_fd)
            self.output_fd = -1
            return MZMLFile(str(output).encode('utf-8'))
        else:
            raise ValueError(f"Unsupported output file extension: {output_ext}. Use .msz or .mzML")

    def get_mz_binary(self, size_t index):
        cdef char* dest = NULL
        cdef size_t out_len = 0
        cdef data_block_t* tmp = _alloc_data_block(self._arguments.blocksize)
        cdef char* mapping_ptr
        cdef size_t start, end
        cdef object mz_array
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
        cdef object inten_array
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

    def __init__(self, bytes path):
        super(MSZFile, self).__init__(path)
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
        return MSZFile(path)
    
    def decompress(self, output: Union[str, PathLike]) -> MZMLFile:
        output = os.fspath(output)
        self.output_fd = self._prepare_output_fd(output)
        _decompress_msz(<char*>self._mapping, self.filesize, self._arguments.get_ptr(), self.output_fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        return MZMLFile(output.encode('utf-8'))

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ) -> Union[MZMLFile, MSZFile]:
        """
        Extract spectra from an MSZ file to mzML format with optional filtering.
        
        Args:
            output: Path to the output file. Must have .mzml extension.
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
        
        Raises:
            ValueError: If output file extension is not supported.
            NotImplementedError: If MSZ output format is requested.
        """
        cdef long* c_indicies = NULL
        cdef long indicies_length = 0
        cdef uint32_t* c_scans = NULL
        cdef long scans_length = 0
        cdef uint16_t c_ms_level = 0
        cdef np.ndarray indicies_arr
        cdef np.ndarray[np.uint32_t, ndim=1] scans_arr
        
        # Determine if output will be mzML or MSZ based on file extension
        output = Path(output).resolve()  # Convert to absolute path
        output_ext = output.suffix.lower()
        
        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a temporary mzML file path, extract to it, then compress to MSZ
            temp_mzml_path = Path(tempfile.gettempdir()) / f"{output.stem}_temp.mzML"
            
            # Delete output file if it already exists to avoid stale data
            if output.exists():
                output.unlink()
            
            # Extract to temporary mzML file
            self.extract(temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)
            
            # Compress the temporary mzML to MSZ
            temp_mzml = None
            try:
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                # Clean up MZMLFile's memory mapping before deleting the file
                if temp_mzml is not None:
                    temp_mzml._cleanup()
                # Clean up temporary file
                if temp_mzml_path.exists():
                    temp_mzml_path.unlink()
        
        elif output_ext == '.mzml':
            # Convert Python lists to C arrays
            # Use _c_long_dtype to match C long type (32-bit on Windows, 64-bit on Linux)
            if indicies is not None:
                indicies_arr = np.array(indicies, dtype=_c_long_dtype)
                c_indicies = <long*>indicies_arr.data
                indicies_length = len(indicies)
            
            if scan_numbers is not None:
                scans_arr = np.array(scan_numbers, dtype=np.uint32)
                c_scans = <uint32_t*>scans_arr.data
                scans_length = len(scan_numbers)
            
            if ms_level is not None:
                c_ms_level = <uint16_t>ms_level
            
            # Prepare output file
            self.output_fd = self._prepare_output_fd(str(output))
            
            # Call the C extraction function
            _extract_msz(
                <char*>self._mapping,
                self.filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                self.output_fd
            )
            
            # Flush and close output
            _flush(self.output_fd)
            _close_file(self.output_fd)
            self.output_fd = -1
            return MZMLFile(str(output).encode('utf-8'))
        else:
            raise ValueError(f"Unsupported output file extension: {output_ext}. Use .msz or .mzML")
        

    def get_mz_binary(self, size_t index):
        cdef char* res = NULL
        cdef size_t out_len = 0
        cdef object mz_array
        cdef double* double_ptr
        cdef float* float_ptr
        
        res = _extract_spectrum_mz(<char*> self._mapping, self._dctx, self._df, self._mz_binary_block_lens, self._footer.mz_binary_pos, self._divisions, index, &out_len, FALSE)
        
        if res == NULL:
            raise ValueError(f"Failed to extract m/z binary for index {index}")
        
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
        cdef object inten_array
        cdef double* double_ptr
        cdef float* float_ptr
        
        res = _extract_spectrum_inten(<char*> self._mapping, self._dctx, self._df, self._inten_binary_block_lens, self._footer.inten_binary_pos, self._divisions, index, &out_len, FALSE)

        if res == NULL:
            raise ValueError(f"Failed to extract intensity binary for index {index}")

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

        if res == NULL:
            raise ValueError(f"Failed to extract XML for index {index}")

        result_str = res.decode('utf-8')

        element = fromstring(result_str)

        return element
    
    def get_header(self) -> str:
        """
        Extract the complete mzML header as a raw string from MSZ file.
        
        This function decompresses the first XML block and extracts the header portion
        (everything from the start of the file to the first spectrum element).
        
        Returns:
            str: The raw XML header string.
            
        Raises:
            RuntimeError: If header extraction fails.
        """
        cdef char* header_data = NULL
        cdef char* decmp_xml = NULL
        cdef size_t header_len = 0
        cdef division_t* first_division = NULL
        cdef block_len_t* xml_blk_len = NULL
        cdef long xml_blk_offset = 0
        
        try:
            # Get the first division
            first_division = self._divisions.divisions[0]
            
            if first_division == NULL:
                raise RuntimeError("Failed to access first division.")
            
            # Get the first XML block
            xml_blk_len = _get_block_by_index(self._xml_block_lens, 0)
            xml_blk_offset = self._footer.xml_pos
            
            # Decompress the XML block
            decmp_xml = <char*>_decmp_block(self._df.xml_decompression_fun, self._dctx, 
                                             self._mapping, xml_blk_offset, xml_blk_len)
            
            if decmp_xml == NULL:
                raise RuntimeError("Failed to decompress XML block for mzML header.")
            
            # Extract the header from decompressed XML
            header_data = _extract_mzml_header(decmp_xml, first_division, &header_len)
            
            if header_data == NULL:
                raise RuntimeError("Failed to extract mzML header.")
            
            # Convert to Python string
            header_str = header_data[:header_len].decode('utf-8', errors='replace')
            
            return header_str
        
        finally:
            # Free the allocated memory
            if header_data != NULL:
                free(header_data)
            if decmp_xml != NULL:
                free(decmp_xml)
    

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
    cdef int output_fd


    def __init__(self, bytes path):
        self._path = path
        self.filesize = _get_filesize(self._path)
        self._fd = _open_input_file(self._path)
        self._mapping = _get_mapping(self._fd)
        self._spectra = None
        self._arguments = RuntimeArguments()
        self._z = _alloc_z_stream()
        self.output_fd = -1
        # Initialize pointers to NULL to prevent undefined behavior
        self._df = NULL
        self._divisions = NULL
        self._positions = NULL


    def __enter__(self):
        # Only open if not already open (e.g., from __init__)
        if self._fd <= 0 or self._mapping == NULL:
            self.filesize = _get_filesize(self._path)
            self._fd = _open_input_file(self._path)
            self._mapping = _get_mapping(self._fd)
        return self
    

    def __exit__(self, exc_type, exc_value, traceback):
        self._cleanup()
    
    def __del__(self):
        # Ensure file handles are released when object is garbage collected
        self._cleanup()
    
    def __reduce__(self):
        return (self.__class__._reopen, (self._path,))

    @staticmethod
    def _reopen(path: bytes):
        fs = _get_filesize(path)
        fd = _open_input_file(path)
        return BaseFile(path)

    def _cleanup(self):
        # On Windows, unmap must happen before closing the file descriptor
        if self._mapping != NULL: 
            _remove_mapping(self._mapping, self.filesize)
            self._mapping = NULL
        
        if self._fd > 0:
            _close_file(self._fd)
            self._fd = -1

        if self.output_fd > 0:
            _close_file(self.output_fd)
            self.output_fd = -1
    

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


    def _prepare_output_fd(self, path: Union[str, PathLike, bytes]) -> int:
        # Use os.fspath() to handle path-like objects (PEP 519)
        path = os.fspath(path)
        if isinstance(path, str):
            path = os.path.expanduser(path)
            path = os.path.abspath(path)
            path = path.encode('utf-8')
        elif isinstance(path, bytes):
            # Handle bytes path - decode, expand, encode back
            path_str = path.decode('utf-8')
            path_str = os.path.expanduser(path_str)
            path_str = os.path.abspath(path_str)
            path = path_str.encode('utf-8')
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
    
    def get_header(self) -> str:
        """
        Extract the complete mzML header as a raw string.
        
        This function extracts the header portion of an mzML file (everything from the start
        of the file to the first spectrum element).
        
        Returns:
            str: The raw XML header string.
            
        Raises:
            RuntimeError: If header extraction fails.
        """
        cdef char* header_data = NULL
        cdef size_t header_len = 0
        cdef division_t* first_division = NULL
        cdef bint use_positions = False
        cdef bytes header_bytes
        
        # Validate mapping is available
        if self._mapping == NULL:
            raise RuntimeError("File mapping is not available. File may be closed.")
        
        try:
            # Check if we should use _positions (MZMLFile) or _divisions (MSZFile)
            if self._divisions == NULL:
                use_positions = True
            elif self._divisions.n_divisions == 0:
                use_positions = True
            
            if use_positions:
                # For MZMLFile, use _positions and mapping directly
                if self._positions == NULL:
                    raise RuntimeError("Failed to access division information (_positions is NULL).")
                first_division = self._positions
            else:
                # For MSZFile, use first division from divisions array
                if self._divisions.divisions == NULL:
                    raise RuntimeError("Failed to access divisions array.")
                first_division = self._divisions.divisions[0]
            
            if first_division == NULL:
                raise RuntimeError("Failed to access first division (first_division is NULL).")
            
            # Validate first_division has required fields
            if first_division.spectra == NULL:
                raise RuntimeError("first_division.spectra is NULL")
            if first_division.xml == NULL:
                raise RuntimeError("first_division.xml is NULL")
            
            header_data = _extract_mzml_header(<char*>self._mapping, first_division, &header_len)
            
            if header_data == NULL:
                raise RuntimeError("Failed to extract mzML header.")
            
            # Convert to Python bytes then string
            header_bytes = header_data[:header_len]
            header_str = header_bytes.decode('utf-8', errors='replace')
            
            return header_str
        
        finally:
            # Free the allocated memory
            if header_data != NULL:
                free(header_data)
    
    def extract_metadata(self, tag_name: str) -> Element:
        """
        Extract and parse a specific XML tag from the mzML file header.
        
        This method extracts the header portion of an mzML file, searches for a specific
        XML tag (e.g., 'referenceableParamGroupList', 'cvList', 'fileDescription'), 
        strips any content outside of it, and parses that XML element.
        
        Parameters:
            tag_name (str): The name of the XML tag to extract (without namespace).
            
        Returns:
            Element: An xml.etree.ElementTree.Element containing the parsed XML tag.
            
        Raises:
            ValueError: If the tag is not found in the header.
            RuntimeError: If header extraction fails.
            ParseError: If XML parsing fails.
            
        Examples:
            >>> with mscompress.read('data.mzml') as f:
            ...     param_groups = f.extract_metadata('referenceableParamGroupList')
            ...     for group in param_groups:
            ...         print(group.attrib)
        """
        header_str = self.get_header()
        
        # Find the tag in the header
        # We need to handle namespace-aware searching
        tag_pattern = f'<{tag_name}'
        tag_start = header_str.find(tag_pattern)
        
        if tag_start == -1:
            raise ValueError(f"Tag '{tag_name}' not found in mzML header.")
        
        # Find the closing tag
        closing_tag = f'</{tag_name}>'
        tag_end = header_str.find(closing_tag, tag_start)
        
        if tag_end == -1:
            raise ValueError(f"Closing tag for '{tag_name}' not found in mzML header.")
        
        # Extract the tag with its closing tag
        tag_end += len(closing_tag)
        tag_content = header_str[tag_start:tag_end]
        
        # Wrap in a minimal XML document to handle namespaces properly
        # Extract namespace declarations from the header
        mzml_start = header_str.find('<mzML')
        if mzml_start != -1:
            mzml_tag_end = header_str.find('>', mzml_start)
            mzml_tag = header_str[mzml_start:mzml_tag_end + 1]
            
            # Extract namespace attributes
            ns_attrs = re.findall(r'xmlns[^=]*="[^"]*"', mzml_tag)
            ns_declaration = ' '.join(ns_attrs) if ns_attrs else ''
            
            # Create a wrapper with proper namespace
            wrapped_xml = f'<root {ns_declaration}>{tag_content}</root>'
        else:
            wrapped_xml = f'<root>{tag_content}</root>'
        
        # Parse the XML
        root = fromstring(wrapped_xml)
        
        # Return the first child (the actual tag we want)
        if len(root) > 0:
            return root[0]
        else:
            raise ValueError(f"Failed to parse '{tag_name}' from header.")


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
                            if param.attrib.get('unitAccession', '') == 'UO:0000031':  # minutes
                                return float(param.attrib['value']) * 60.0
                            else:  # seconds
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
            mz = self.mz
            intensity = self.intensity
            if len(mz) != len(intensity):
                raise ValueError(f"Mismatch in array lengths: mz has {len(mz)} elements, intensity has {len(intensity)} elements for spectrum {self.index}")
            return np.column_stack((mz, intensity))

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
