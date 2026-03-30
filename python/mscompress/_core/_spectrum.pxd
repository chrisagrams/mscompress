from mscompress._core._base cimport BaseFile

cdef class Spectra:
    cdef BaseFile _f
    cdef object _df
    cdef object _positions
    cdef object _cache
    cdef int _index
    cdef size_t length

    cdef Spectrum _compute_spectrum(self, size_t index)

cdef class Spectrum:
    cdef unsigned long long _index
    cdef unsigned int _scan
    cdef unsigned short _ms_level
    cdef float _retention_time
    cdef BaseFile _file
    cdef object _mz
    cdef object _intensity
    cdef object _xml
