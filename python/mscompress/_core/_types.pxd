from mscompress._core._bindings cimport data_format_t, data_positions_t, division_t, Arguments, data_block_t

cdef class RuntimeArguments:
    cdef Arguments _arguments
    cdef Arguments* get_ptr(self)

    @staticmethod
    cdef RuntimeArguments from_ptr(Arguments* ptr)

cdef class DataBlock:
    cdef data_block_t _data_block

cdef class DataFormat:
    cdef data_format_t _data_format

    @staticmethod
    cdef DataFormat from_ptr(data_format_t* ptr)

cdef class DataPositions:
    cdef data_positions_t *data_positions

    @staticmethod
    cdef DataPositions from_ptr(data_positions_t* ptr)

cdef class Division:
    cdef division_t* _division

    @staticmethod
    cdef Division from_ptr(division_t* ptr)
