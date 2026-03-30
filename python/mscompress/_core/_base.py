import cython
import os
import re
import numpy as np

from typing import Union
from os import PathLike
from pathlib import Path
from xml.etree.ElementTree import fromstring, Element, ParseError

from cython.cimports.libc.stdlib import malloc, free
from cython.cimports.mscompress._core._bindings import (
    data_format_t,
    divisions_t,
    division_t,
    z_stream,
    _get_filesize,
    _open_input_file,
    _open_output_file,
    _close_file,
    _remove_mapping,
    _get_mapping,
    _alloc_z_stream,
    _dealloc_z_stream,
    _dealloc_df,
    _dealloc_division,
    _extract_mzml_header,
)

from cython.cimports.mscompress._core._types import RuntimeArguments, DataFormat, Division


@cython.cclass
class BaseFile:
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
    # Attributes declared in _base.pxd — do not re-declare here.

    def __init__(self, path: bytes):
        self._path = path
        if not os.path.exists(path):
            raise FileNotFoundError(f"File not found: {os.fsdecode(path)}")
        self.filesize = _get_filesize(self._path)
        self._fd = _open_input_file(self._path)
        self._mapping = _get_mapping(self._fd)
        self._spectra = None
        self._arguments = RuntimeArguments()
        self._z = _alloc_z_stream()
        self.output_fd = -1
        # Initialize pointers to NULL to prevent undefined behavior
        self._df = cython.NULL
        self._divisions = cython.NULL
        self._positions = cython.NULL

    def __enter__(self):
        # Only open if not already open (e.g., from __init__)
        if self._fd <= 0 or self._mapping == cython.NULL:
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
        # Unmap file mapping if exists
        if self._mapping != cython.NULL:
            _remove_mapping(self._mapping, self.filesize)
            self._mapping = cython.NULL

        # Close input file descriptor if open
        if self._fd >= 0:
            _close_file(self._fd)
            self._fd = -1

        # Close output file descriptor if open
        if self.output_fd >= 0:
            _close_file(self.output_fd)
            self.output_fd = -1

        # Free zlib z_stream
        if self._z != cython.NULL:
            _dealloc_z_stream(self._z)
            self._z = cython.NULL

        # Free data_format_t
        if self._df != cython.NULL:
            _dealloc_df(self._df)
            self._df = cython.NULL

        # Free division_t _positions (always fully malloc'd from scan_mzml or flatten_divisions)
        if self._positions != cython.NULL:
            _dealloc_division(self._positions)
            self._positions = cython.NULL

    @property
    def path(self) -> bytes:
        return self._path

    # filesize is exposed via 'cdef readonly' in _base.pxd

    @property
    def format(self) -> DataFormat:
        return DataFormat.from_ptr(self._df)

    @property
    def spectra(self):
        if self._spectra is None:
            from mscompress._core._spectrum import Spectra
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
        output_fd: cython.int = _open_output_file(path)
        return output_fd

    def get_mz_binary(self, index: cython.size_t):
        raise NotImplementedError("This method should be overridden in subclasses")

    def get_inten_binary(self, index: cython.size_t):
        raise NotImplementedError("This method should be overridden in subclasses")

    def get_xml(self, index: cython.size_t):
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
        header_data: cython.p_char = cython.NULL
        header_len: cython.size_t = 0
        first_division: cython.pointer(division_t) = cython.NULL
        use_positions: cython.bint = False
        header_bytes: bytes

        # Validate mapping is available
        if self._mapping == cython.NULL:
            raise RuntimeError("File mapping is not available. File may be closed.")

        try:
            # Check if we should use _positions (MZMLFile) or _divisions (MSZFile)
            if self._divisions == cython.NULL:
                use_positions = True
            elif self._divisions.n_divisions == 0:
                use_positions = True

            if use_positions:
                # For MZMLFile, use _positions and mapping directly
                if self._positions == cython.NULL:
                    raise RuntimeError("Failed to access division information (_positions is NULL).")
                first_division = self._positions
            else:
                # For MSZFile, use first division from divisions array
                if self._divisions.divisions == cython.NULL:
                    raise RuntimeError("Failed to access divisions array.")
                first_division = self._divisions.divisions[0]

            if first_division == cython.NULL:
                raise RuntimeError("Failed to access first division (first_division is NULL).")

            # Validate first_division has required fields
            if first_division.spectra == cython.NULL:
                raise RuntimeError("first_division.spectra is NULL")
            if first_division.xml == cython.NULL:
                raise RuntimeError("first_division.xml is NULL")

            header_data = _extract_mzml_header(cython.cast(cython.p_char, self._mapping), first_division, cython.address(header_len))

            if header_data == cython.NULL:
                raise RuntimeError("Failed to extract mzML header.")

            # Convert to Python bytes then string
            header_bytes = header_data[:header_len]
            header_str = header_bytes.decode('utf-8', errors='replace')

            return header_str

        finally:
            # Free the allocated memory
            if header_data != cython.NULL:
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
