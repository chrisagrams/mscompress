import cython
import os
import shutil
import tempfile
import numpy as np

from typing import Union, Iterator, Optional
from os import PathLike
from pathlib import Path
from xml.etree.ElementTree import fromstring

from cython.cimports.libc.stdlib import free
from cython.cimports.libc.string import memcpy
from cython.cimports.numpy import ndarray as cnp_ndarray
from cython.cimports.mscompress._core._bindings import (
    Arguments,
    data_format_t,
    divisions_t,
    division_t,
    footer_t,
    block_len_t,
    block_len_queue_t,
    uint32_t,
    uint16_t,
    ZSTD_DCtx,
    ZSTD_freeDCtx,
    FALSE,
    _get_header_df,
    _read_footer,
    _read_divisions,
    _flatten_divisions,
    _alloc_dctx,
    _read_block_len_queue,
    _set_decompress_runtime_variables,
    _dealloc_block_len_queue,
    _dealloc_read_divisions,
    _decompress_msz,
    _extract_msz,
    _extract_spectrum_mz,
    _extract_spectrum_inten,
    _extract_spectra,
    _extract_mzml_header,
    _get_block_by_index,
    _decmp_block,
    _flush,
    _close_file,
    _32f_,
    _64d_,
)

from cython.cimports.mscompress._core._base import BaseFile
from mscompress._core._utils import _pipe_stream, _c_long_dtype


@cython.cclass
class MSZFile(BaseFile):
    _footer: cython.pointer(footer_t)
    _dctx: cython.pointer(ZSTD_DCtx)
    _xml_block_lens: cython.pointer(block_len_queue_t)
    _mz_binary_block_lens: cython.pointer(block_len_queue_t)
    _inten_binary_block_lens: cython.pointer(block_len_queue_t)

    def __init__(self, path: bytes):
        super().__init__(path)
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

    def _cleanup(self):
        """Free MSZFile-specific resources before calling parent cleanup"""

        # Free ZSTD_DCtx
        if self._dctx != cython.NULL:
            ZSTD_freeDCtx(self._dctx)
            self._dctx = cython.NULL

        # Free block length queues
        if self._xml_block_lens != cython.NULL:
            _dealloc_block_len_queue(self._xml_block_lens)
            self._xml_block_lens = cython.NULL
        if self._mz_binary_block_lens != cython.NULL:
            _dealloc_block_len_queue(self._mz_binary_block_lens)
            self._mz_binary_block_lens = cython.NULL
        if self._inten_binary_block_lens != cython.NULL:
            _dealloc_block_len_queue(self._inten_binary_block_lens)
            self._inten_binary_block_lens = cython.NULL

        # Free divisions from read_divisions (mmap-backed, only free struct wrappers)
        if self._divisions != cython.NULL:
            _dealloc_read_divisions(self._divisions)
            self._divisions = cython.NULL

        # _footer points to mmap'd memory, don't free

        # Call parent cleanup
        super()._cleanup()

    def decompress(self, output: Union[str, PathLike]):
        output = os.fspath(output)
        self.output_fd = self._prepare_output_fd(output)
        rv: cython.int = _decompress_msz(cython.cast(cython.p_char, self._mapping), self.filesize, self._arguments.get_ptr(), self.output_fd)
        _flush(self.output_fd)
        _close_file(self.output_fd)
        self.output_fd = -1
        if rv != 0:
            raise RuntimeError("Decompression failed: error during decompression.")
        from mscompress._core._mzml import MZMLFile
        return MZMLFile(output.encode('utf-8'))

    def _decompress_to_fd(self, write_fd: cython.int):
        """Internal helper: run decompression pipeline writing to the given fd."""
        rv: cython.int
        mapping: cython.p_char = cython.cast(cython.p_char, self._mapping)
        filesize: cython.size_t = self.filesize
        args: cython.pointer(Arguments) = self._arguments.get_ptr()
        # Release the GIL so the reader thread can drain the pipe.
        with cython.nogil:
            rv = _decompress_msz(mapping, filesize, args, write_fd)
            _flush(write_fd)
        if rv != 0:
            raise RuntimeError("Decompression failed: error during decompression.")

    def decompress_stream(self, chunk_size: int = 1_048_576) -> Iterator[bytes]:
        """Decompress this MSZ file and yield the mzML output as byte chunks.

        Args:
            chunk_size: Number of bytes to read per iteration (default 1MB).

        Yields:
            bytes: Chunks of decompressed mzML data.
        """
        return _pipe_stream(self._decompress_to_fd, (), chunk_size=chunk_size)

    def extract(
        self,
        output: Union[str, PathLike],
        indicies: Optional[list[int]] = None,
        scan_numbers: Optional[list[int]] = None,
        ms_level: Optional[int] = None
    ):
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
        c_indicies: cython.pointer(cython.long) = cython.NULL
        indicies_length: cython.long = 0
        c_scans: cython.pointer(uint32_t) = cython.NULL
        scans_length: cython.long = 0
        c_ms_level: uint16_t = 0

        # Determine if output will be mzML or MSZ based on file extension
        output = Path(output).resolve()  # Convert to absolute path
        output_ext = output.suffix.lower()

        if output_ext == '.msz':
            # Output as MSZ (compressed)
            # Create a unique temporary directory to avoid collisions in concurrent extractions
            temp_dir = tempfile.mkdtemp()
            temp_mzml_path = Path(temp_dir) / f"{output.stem}_temp.mzML"

            # Delete output file if it already exists to avoid stale data
            if output.exists():
                output.unlink()

            # Extract to temporary mzML file
            temp_mzml = None
            try:
                self.extract(temp_mzml_path, indicies=indicies, scan_numbers=scan_numbers, ms_level=ms_level)

                # Compress the temporary mzML to MSZ
                from mscompress._core._mzml import MZMLFile
                temp_mzml = MZMLFile(str(temp_mzml_path).encode('utf-8'))
                return temp_mzml.compress(str(output))
            finally:
                # Clean up MZMLFile's memory mapping before deleting the temp directory
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

            # Call the C extraction function
            _extract_msz(
                cython.cast(cython.p_char, self._mapping),
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
            from mscompress._core._mzml import MZMLFile
            return MZMLFile(str(output).encode('utf-8'))
        else:
            raise ValueError(f"Unsupported output file extension: {output_ext}. Use .msz or .mzML")

    def _extract_msz_to_fd(self, indicies_arr, scans_arr, c_ms_level: uint16_t, write_fd: cython.int):
        """Internal helper: run MSZ extraction writing to the given fd."""
        c_indicies: cython.pointer(cython.long) = cython.NULL
        indicies_length: cython.long = 0
        c_scans: cython.pointer(uint32_t) = cython.NULL
        scans_length: cython.long = 0
        mapping: cython.p_char = cython.cast(cython.p_char, self._mapping)
        filesize: cython.size_t = self.filesize

        if indicies_arr is not None:
            c_indicies = cython.cast(cython.pointer(cython.long), cython.cast(cnp_ndarray, indicies_arr).data)
            indicies_length = len(indicies_arr)

        if scans_arr is not None:
            c_scans = cython.cast(cython.pointer(uint32_t), cython.cast(cnp_ndarray, scans_arr).data)
            scans_length = len(scans_arr)

        # Release the GIL so the reader thread can drain the pipe.
        with cython.nogil:
            _extract_msz(
                mapping,
                filesize,
                c_indicies,
                indicies_length,
                c_scans,
                scans_length,
                c_ms_level,
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
        """Extract spectra from this MSZ file and yield mzML output as byte chunks.

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

        return _pipe_stream(self._extract_msz_to_fd, (indicies_arr, scans_arr, c_ms_level), chunk_size=chunk_size)

    def get_mz_binary(self, index: cython.size_t):
        res: cython.p_char = cython.NULL
        out_len: cython.size_t = 0

        res = _extract_spectrum_mz(cython.cast(cython.p_char, self._mapping), self._dctx, self._df, self._mz_binary_block_lens, self._footer.mz_binary_pos, self._divisions, index, cython.address(out_len), FALSE)

        if res == cython.NULL:
            raise ValueError(f"Failed to extract m/z binary for index {index}")

        try:
            if self._df.source_mz_fmt == _64d_:
                count = int((out_len) / 8)
                double_ptr: cython.pointer(cython.double) = cython.cast(cython.pointer(cython.double), res)
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    mz_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, mz_array_64).data), cython.cast(cython.pointer(cython.void), double_ptr), count * 8)
                    return mz_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_mz_fmt == _32f_:
                count = int((out_len) / 4)
                float_ptr: cython.pointer(cython.float) = cython.cast(cython.pointer(cython.float), res)
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    mz_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, mz_array_32).data), cython.cast(cython.pointer(cython.void), float_ptr), count * 4)
                    return mz_array_32
                else:
                    return np.array([], dtype=np.float32)
        finally:
            # Free the C buffer returned by _extract_spectrum_mz
            free(res)

    def get_inten_binary(self, index: cython.size_t):
        res: cython.p_char = cython.NULL
        out_len: cython.size_t = 0

        res = _extract_spectrum_inten(cython.cast(cython.p_char, self._mapping), self._dctx, self._df, self._inten_binary_block_lens, self._footer.inten_binary_pos, self._divisions, index, cython.address(out_len), FALSE)

        if res == cython.NULL:
            raise ValueError(f"Failed to extract intensity binary for index {index}")

        try:
            if self._df.source_inten_fmt == _64d_:
                count = int((out_len) / 8)
                double_ptr: cython.pointer(cython.double) = cython.cast(cython.pointer(cython.double), res)
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    inten_array_64 = np.empty(count, dtype=np.float64)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, inten_array_64).data), cython.cast(cython.pointer(cython.void), double_ptr), count * 8)
                    return inten_array_64
                else:
                    return np.array([], dtype=np.float64)
            elif self._df.source_inten_fmt == _32f_:
                count = int((out_len) / 4)
                float_ptr: cython.pointer(cython.float) = cython.cast(cython.pointer(cython.float), res)
                if out_len > 0:
                    # Copy data into numpy-owned array to avoid memory leak
                    inten_array_32 = np.empty(count, dtype=np.float32)
                    memcpy(cython.cast(cython.pointer(cython.void), cython.cast(cnp_ndarray, inten_array_32).data), cython.cast(cython.pointer(cython.void), float_ptr), count * 4)
                    return inten_array_32
                else:
                    return np.array([], dtype=np.float32)
        finally:
            # Free the C buffer returned by _extract_spectrum_inten
            free(res)

    def get_xml(self, index: cython.size_t):
        res: cython.p_char = cython.NULL
        out_len: cython.size_t = 0
        xml_pos: cython.long
        mz_pos: cython.long
        inten_pos: cython.long
        mz_fmt: cython.int
        inten_fmt: cython.int

        xml_pos = cython.cast(cython.long, self._footer.xml_pos)
        mz_pos = cython.cast(cython.long, self._footer.mz_binary_pos)
        inten_pos = cython.cast(cython.long, self._footer.inten_binary_pos)
        mz_fmt = cython.cast(cython.int, self._footer.mz_fmt)
        inten_fmt = cython.cast(cython.int, self._footer.inten_fmt)

        res = _extract_spectra(
            cython.cast(cython.p_char, self._mapping), self._dctx, self._df,
            self._xml_block_lens, self._mz_binary_block_lens,
            self._inten_binary_block_lens, xml_pos, mz_pos,
            inten_pos, mz_fmt, inten_fmt, self._divisions, index, cython.address(out_len)
        )

        if res == cython.NULL:
            raise ValueError(f"Failed to extract XML for index {index}")

        try:
            result_str = res.decode('utf-8')
        finally:
            # Free the C buffer returned by _extract_spectra
            free(res)

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
        header_data: cython.p_char = cython.NULL
        decmp_xml: cython.p_char = cython.NULL
        header_len: cython.size_t = 0
        first_division: cython.pointer(division_t) = cython.NULL
        xml_blk_len: cython.pointer(block_len_t) = cython.NULL
        xml_blk_offset: cython.long = 0

        try:
            # Get the first division
            first_division = self._divisions.divisions[0]

            if first_division == cython.NULL:
                raise RuntimeError("Failed to access first division.")

            # Get the first XML block
            xml_blk_len = _get_block_by_index(self._xml_block_lens, 0)
            xml_blk_offset = self._footer.xml_pos

            # Decompress the XML block
            decmp_xml = cython.cast(cython.p_char, _decmp_block(self._df.xml_decompression_fun, self._dctx,
                                             self._mapping, xml_blk_offset, xml_blk_len))

            if decmp_xml == cython.NULL:
                raise RuntimeError("Failed to decompress XML block for mzML header.")

            # Extract the header from decompressed XML
            header_data = _extract_mzml_header(decmp_xml, first_division, cython.address(header_len))

            if header_data == cython.NULL:
                raise RuntimeError("Failed to extract mzML header.")

            # Convert to Python string
            header_str = header_data[:header_len].decode('utf-8', errors='replace')

            return header_str

        finally:
            # Free the allocated memory
            if header_data != cython.NULL:
                free(header_data)
            if decmp_xml != cython.NULL:
                free(decmp_xml)
