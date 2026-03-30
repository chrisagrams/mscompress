import cython
import os
import shutil
import tempfile
import warnings
import numpy as np

from typing import Union, Iterator, Optional
from os import PathLike
from pathlib import Path
from xml.etree.ElementTree import fromstring

from cython.cimports.libc.stdlib import malloc, free
from cython.cimports.libc.string import memcpy
from cython.cimports.numpy import ndarray as cnp_ndarray
from cython.cimports.mscompress._core._bindings import (
    Arguments,
    data_format_t,
    divisions_t,
    division_t,
    data_block_t,
    uint32_t,
    uint16_t,
    _pattern_detect,
    _scan_mzml,
    _set_compress_runtime_variables,
    _determine_n_divisions,
    _create_divisions,
    _get_division_size_max,
    _compress_mzml,
    _flush,
    _close_file,
    _dealloc_divisions,
    _extract_mzml_filtered,
    _alloc_data_block,
    _dealloc_data_block,
    _32f_,
    _64d_,
    ZLIB_SIZE_OFFSET,
)

from cython.cimports.mscompress._core._base import BaseFile
from mscompress._core._utils import _pipe_stream, _c_long_dtype


@cython.cclass
class MZMLFile(BaseFile):
    def __init__(self, path: bytes):
        super().__init__(path)
        if self._mapping is cython.NULL:
            raise RuntimeError("File mapping is NULL. Filesize might be 0.")
        self._df = _pattern_detect(cython.cast(cython.p_char, self._mapping))
        if self._df is cython.NULL:
            raise RuntimeError("pattern_detect returned NULL. Failed to detect mzML pattern.")

        self._positions = _scan_mzml(cython.cast(cython.p_char, self._mapping), self._df, self.filesize, 7)  # 7 = MSLEVEL|SCANNUM|RETTIME
        if self._positions is cython.NULL:
            raise RuntimeError("scan_mzml returned NULL. The file might be empty or invalid.")
        _set_compress_runtime_variables(self._arguments.get_ptr(), self._df)

    @staticmethod
    def _reopen(path: bytes):
        return MZMLFile(path)

    def _cleanup(self):
        """Free MZMLFile-specific resources before calling parent cleanup"""
        # Free divisions created by _prepare_divisions (fully malloc'd)
        if self._divisions != cython.NULL:
            _dealloc_divisions(self._divisions)
            self._divisions = cython.NULL
        # Call parent cleanup
        super()._cleanup()

    def _prepare_divisions(self):
        n_divisions: cython.long = _determine_n_divisions(self._positions.size, self._arguments.blocksize)
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
            effective_divisions = self._arguments.threads
            if effective_divisions > self._positions.mz.total_spec:
                warnings.warn(
                    f"n_threads ({effective_divisions}) > total_spec ({self._positions.mz.total_spec}). "
                    f"Setting n_divisions to {self._positions.mz.total_spec}"
                )
                effective_divisions = self._positions.mz.total_spec
                if effective_divisions == 0:
                    effective_divisions = 1
            self._divisions = _create_divisions(self._positions, effective_divisions)
            # If we have more threads than divisions, increase the blocksize to max division size
            self._arguments.blocksize = _get_division_size_max(self._divisions)

    def compress(self, output: Union[str, PathLike]):
        output = os.fspath(output)
        self._prepare_divisions()
        self.output_fd = self._prepare_output_fd(output)
        rv: cython.int = _compress_mzml(cython.cast(cython.p_char, self._mapping), self.filesize, self._arguments.get_ptr(), self._df, self._divisions, self.output_fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        if rv != 0:
            raise RuntimeError("Compression failed: write error during compression.")
        from mscompress._core._msz import MSZFile
        return MSZFile(output.encode('utf-8'))

    def _compress_to_fd(self, write_fd: cython.int):
        """Internal helper: run compression pipeline writing to the given fd."""
        rv: cython.int
        mapping: cython.p_char = cython.cast(cython.p_char, self._mapping)
        filesize: cython.size_t = self.filesize
        args: cython.pointer(Arguments) = self._arguments.get_ptr()
        df: cython.pointer(data_format_t) = self._df
        divisions: cython.pointer(divisions_t) = self._divisions
        # Release the GIL so the reader thread can drain the pipe; without this,
        # write() blocks on a full pipe while holding the GIL -> deadlock.
        with cython.nogil:
            rv = _compress_mzml(mapping, filesize, args, df, divisions, write_fd)
            _flush(write_fd)
        if rv != 0:
            raise RuntimeError("Compression failed: write error during compression.")

    def compress_stream(self, chunk_size: int = 1_048_576) -> Iterator[bytes]:
        """Compress this mzML file and yield the MSZ output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of compressed MSZ data.
        """
        self._prepare_divisions()
        return _pipe_stream(self._compress_to_fd, (), chunk_size=chunk_size)

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ):
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
        c_indicies: cython.pointer(cython.long) = cython.NULL
        indicies_length: cython.long = 0
        c_scans: cython.pointer(uint32_t) = cython.NULL
        scans_length: cython.long = 0
        c_ms_level: uint16_t = 0

        output = Path(output).resolve()
        output_ext = output.suffix.lower()

        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a unique temporary directory to avoid collisions in concurrent extractions
            temp_dir = tempfile.mkdtemp()
            temp_mzml_path = Path(temp_dir) / f"{output.stem}_temp.mzML"

            # Delete output file if it already exists
            if output.exists():
                output.unlink()

            # Extract to temporary mzML file
            temp_mzml = None
            try:
                self.extract(temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)

                # Compress the temporary mzML to MSZ
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                if temp_mzml is not None:
                    temp_mzml._cleanup()
                shutil.rmtree(temp_dir, ignore_errors=True)

        elif output_ext == '.mzml':
            # Convert Python lists to C arrays
            # Use _c_long_dtype to match C long type (32-bit on Windows, 64-bit on Linux)
            if indicies is not None:
                indicies_arr = np.array(indicies, dtype=_c_long_dtype)
                c_indicies = cython.cast(cython.pointer(cython.long), cython.cast(cnp_ndarray, indicies_arr).data)
                indicies_length = len(indicies)

            if scan_numbers is not None:
                scans_arr = np.array(scan_numbers, dtype=np.uint32)
                c_scans = cython.cast(cython.pointer(uint32_t), cython.cast(cnp_ndarray, scans_arr).data)
                scans_length = len(scan_numbers)

            if ms_level is not None:
                c_ms_level = cython.cast(uint16_t, ms_level)

            # Prepare output file
            self.output_fd = self._prepare_output_fd(str(output))

            _extract_mzml_filtered(
                cython.cast(cython.p_char, self._mapping),
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

    def _extract_mzml_to_fd(self, indicies_arr, scans_arr, c_ms_level: uint16_t, write_fd: cython.int):
        """Internal helper: run mzML extraction writing to the given fd."""
        c_indicies: cython.pointer(cython.long) = cython.NULL
        indicies_length: cython.long = 0
        c_scans: cython.pointer(uint32_t) = cython.NULL
        scans_length: cython.long = 0
        mapping: cython.p_char = cython.cast(cython.p_char, self._mapping)
        filesize: cython.size_t = self.filesize
        positions: cython.pointer(division_t) = self._positions

        if indicies_arr is not None:
            c_indicies = cython.cast(cython.pointer(cython.long), cython.cast(cnp_ndarray, indicies_arr).data)
            indicies_length = len(indicies_arr)

        if scans_arr is not None:
            c_scans = cython.cast(cython.pointer(uint32_t), cython.cast(cnp_ndarray, scans_arr).data)
            scans_length = len(scans_arr)

        # Release the GIL so the reader thread can drain the pipe.
        with cython.nogil:
            _extract_mzml_filtered(
                mapping,
                filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
                positions,
                write_fd
            )
            _flush(write_fd)

    def extract_stream(
        self,
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None,
        chunk_size: int = 1_048_576
    ) -> Iterator[bytes]:
        """Extract spectra from this mzML file and yield mzML output as byte chunks.

        Args:
            indicies: Optional list of spectrum indices to extract.
            scan_numbers: Optional list of scan numbers to extract.
            ms_level: Optional MS level to filter by (e.g., 1 for MS1, 2 for MS2).
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of extracted mzML data.
        """
        indicies_arr = None
        scans_arr = None
        c_ms_level: uint16_t = 0

        if indicies is not None:
            indicies_arr = np.array(indicies, dtype=_c_long_dtype)
        if scan_numbers is not None:
            scans_arr = np.array(scan_numbers, dtype=np.uint32)
        if ms_level is not None:
            c_ms_level = cython.cast(uint16_t, ms_level)

        return _pipe_stream(self._extract_mzml_to_fd, (indicies_arr, scans_arr, c_ms_level), chunk_size=chunk_size)

    def get_mz_binary(self, index: cython.size_t):
        dest: cython.p_char = cython.NULL
        decode_output: cython.p_char = cython.NULL
        out_len: cython.size_t = 0
        tmp: cython.pointer(data_block_t) = _alloc_data_block(self._arguments.blocksize)
        mapping_ptr: cython.p_char
        start: cython.size_t
        end: cython.size_t

        start = self._positions.mz.start_positions[index]
        end = self._positions.mz.end_positions[index]

        mapping_ptr = cython.cast(cython.p_char, self._mapping)
        mapping_ptr += start

        self._df.decode_source_compression_mz_fun(self._z, mapping_ptr, end - start, cython.address(dest), cython.address(out_len), tmp)
        decode_output = dest  # Save pointer to decode function's allocation

        dest += ZLIB_SIZE_OFFSET  # Skip zlib header

        try:
            if self._df.source_mz_fmt == _64d_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
                double_ptr: cython.pointer(cython.double) = cython.cast(cython.pointer(cython.double), dest)

                if out_len > 0:
                    # Copy data into numpy-owned array
                    mz_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, mz_array_64).data), cython.cast(cython.pointer(cython.void), double_ptr), count * 8)
                    return mz_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_mz_fmt == _32f_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
                float_ptr: cython.pointer(cython.float) = cython.cast(cython.pointer(cython.float), dest)

                if out_len > 0:
                    # Copy data into numpy-owned array
                    mz_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, mz_array_32).data), cython.cast(cython.pointer(cython.void), float_ptr), count * 4)
                    return mz_array_32
                else:
                    return np.array([], dtype=np.float32)
            else:
                raise NotImplementedError("Data format not implemented.")
        finally:
            free(decode_output)
            _dealloc_data_block(tmp)

    def get_inten_binary(self, index: cython.size_t):
        dest: cython.p_char = cython.NULL
        decode_output: cython.p_char = cython.NULL
        out_len: cython.size_t = 0
        tmp: cython.pointer(data_block_t) = _alloc_data_block(self._arguments.blocksize)
        mapping_ptr: cython.p_char
        start: cython.size_t
        end: cython.size_t

        start = self._positions.inten.start_positions[index]
        end = self._positions.inten.end_positions[index]

        mapping_ptr = cython.cast(cython.p_char, self._mapping)
        mapping_ptr += start

        self._df.decode_source_compression_inten_fun(self._z, mapping_ptr, end - start, cython.address(dest), cython.address(out_len), tmp)
        decode_output = dest  # Save pointer to decode function's allocation

        dest += ZLIB_SIZE_OFFSET  # Skip zlib header

        try:
            if self._df.source_inten_fmt == _64d_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 8)
                double_ptr: cython.pointer(cython.double) = cython.cast(cython.pointer(cython.double), dest)

                if out_len > 0:
                    # Copy data into numpy-owned array
                    inten_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, inten_array_64).data), cython.cast(cython.pointer(cython.void), double_ptr), count * 8)
                    return inten_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_inten_fmt == _32f_:
                count = int((out_len - ZLIB_SIZE_OFFSET) / 4)
                float_ptr: cython.pointer(cython.float) = cython.cast(cython.pointer(cython.float), dest)

                if out_len > 0:
                    # Copy data into numpy-owned array
                    inten_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, inten_array_32).data), cython.cast(cython.pointer(cython.void), float_ptr), count * 4)
                    return inten_array_32
                else:
                    return np.array([], dtype=np.float32)
            else:
                raise NotImplementedError("Data format not implemented.")
        finally:
            free(decode_output)
            _dealloc_data_block(tmp)

    def get_xml(self, index: cython.size_t):
        res: cython.p_char
        mapping_ptr: cython.p_char

        start = self._positions.spectra.start_positions[index]
        end = self._positions.spectra.end_positions[index]

        size = end - start

        mapping_ptr = cython.cast(cython.p_char, self._mapping)
        mapping_ptr += start

        res = cython.cast(cython.p_char, malloc(size + 1))

        memcpy(res, cython.cast(cython.pointer(cython.void), mapping_ptr), size)

        res[size] = b'\0'

        result_str = res.decode('utf-8')

        free(res)

        element = fromstring(result_str)

        return element
