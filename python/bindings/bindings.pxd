cdef extern from "stdint.h":
    ctypedef unsigned int uint32_t

cdef extern from "../vendor/zlib/zlib.h":
    ctypedef struct z_stream:
        pass

cdef extern from "../vendor/zstd/lib/zstd.h":
    ctypedef struct ZSTD_CCtx:
        pass

    ctypedef struct ZSTD_DCtx:
        pass
    
cdef extern from "../src/mscompress.h":
    cdef struct data_format_t
    cdef struct data_block_t