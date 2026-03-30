from mscompress._core._bindings cimport data_format_t, divisions_t, division_t, z_stream
from mscompress._core._types cimport RuntimeArguments

cdef class BaseFile:
    cdef bytes _path
    cdef readonly size_t filesize
    cdef int _fd
    cdef void* _mapping
    cdef data_format_t* _df
    cdef divisions_t* _divisions
    cdef division_t* _positions
    cdef object _spectra
    cdef RuntimeArguments _arguments
    cdef z_stream* _z
    cdef int output_fd
