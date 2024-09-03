cdef extern from "stdint.h":
    ctypedef unsigned int uint64_t
    ctypedef unsigned int uint32_t
    ctypedef unsigned int uint16_t

cdef extern from "../vendor/zlib/zlib.h":
    ctypedef struct z_stream:
        pass

cdef extern from "../vendor/zstd/lib/zstd.h":
    ctypedef struct ZSTD_CCtx:
        pass

    ctypedef struct ZSTD_DCtx:
        pass

cdef extern from "../src/mscompress.h":
    int TRUE
    int FALSE
    int _ZSTD_compression_
    int ZLIB_SIZE_OFFSET
    int _32f_
    int _64d_
    
    ctypedef void (*Algo)(void*)
    ctypedef Algo (*Algo_ptr)()

    ctypedef void (*decode_fun)(z_stream *, char *, size_t, char **, size_t *, data_block_t *)
    ctypedef decode_fun (*decode_fun_ptr)()

    ctypedef void (*encode_fun)(z_stream *, char **, size_t, char *, size_t *)
    ctypedef encode_fun (*encode_fun_ptr)()

    ctypedef void* (*compression_fun)(ZSTD_CCtx* , void* , size_t , size_t* , int )
    ctypedef compression_fun (*compression_fun_ptr)()

    ctypedef void* (*decompression_fun)(ZSTD_DCtx *, void *, size_t , size_t )
    ctypedef decompression_fun (*decompression_fun_ptr)()

    ctypedef struct Arguments:
        int threads
        char* mz_lossy
        char* int_lossy
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
    
    ctypedef struct block_len_t:
        size_t original_size
        size_t compressed_size
        block_len_t* next

        char* cache

        char* encoded_cache
        uint32_t encoded_cache_fmt
        uint64_t encoded_cache_len
        size_t* encoded_cache_lens

    ctypedef struct block_len_queue_t:
        block_len_t* head
        block_len_t* tail

        int populated
    
    ctypedef struct data_format_t:
        uint32_t source_mz_fmt
        uint32_t source_inten_fmt
        uint32_t source_compression
        uint32_t source_total_spec

        uint32_t target_xml_format
        uint32_t target_mz_format
        uint32_t target_inten_format

        decode_fun decode_source_compression_mz_fun
        decode_fun decode_source_compression_inten_fun
        encode_fun encode_source_compression_mz_fun
        encode_fun encode_source_compression_inten_fun

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

    ctypedef struct divisions_t:
        division_t** divisions
        int n_divisions
    
    ctypedef struct footer_t:
        uint64_t xml_pos
        uint64_t mz_binary_pos
        uint64_t inten_binary_pos
        uint64_t xml_blk_pos
        uint64_t mz_binary_blk_pos
        uint64_t inten_binary_blk_pos
        uint64_t divisions_t_pos
        size_t num_spectra
        uint64_t original_filesize
        int n_divisions
        int magic_tag
        int mz_fmt
        int inten_fmt

    int verbose
    int _get_num_threads "get_num_threads"()
    int _open_input_file "open_input_file"(char* input_path)
    int _open_output_file "open_output_file"(char* path)
    int _close_file "close_file"(int fd)
    int _remove_mapping "remove_mapping"(void* addr, size_t length)
    int _flush "flush"(int fd)
    size_t _get_filesize "get_filesize"(char* path)
    void* _get_mapping "get_mapping"(int fd)
    int _determine_filetype "determine_filetype"(void* input_map, size_t input_length)

    data_format_t* _pattern_detect "pattern_detect"(char* input_map)
    data_format_t* _get_header_df "get_header_df"(void* input_map)

    division_t* _scan_mzml "scan_mzml"(char* input_map, data_format_t* df, long end, int flags)
    long _determine_n_divisions "determine_n_divisions"(long filesize, long blocksize)
    divisions_t* _create_divisions "create_divisions"(division_t* div, long n_divisions)
    long _get_division_size_max "get_division_size_max"(divisions_t* divisions)
    void _set_compress_runtime_variables "set_compress_runtime_variables"(Arguments* args, data_format_t* df)
    void _set_decompress_runtime_variables "set_decompress_runtime_variables"(data_format_t* df, footer_t* msz_footer)
    data_block_t* _alloc_data_block "alloc_data_block"(size_t max_size)
    void _dealloc_data_block "dealloc_data_block"(data_block_t* db)
    z_stream* _alloc_z_stream "alloc_z_stream"()
    ZSTD_CCtx* _alloc_cctx "alloc_cctx"()
    ZSTD_DCtx* _alloc_dctx "alloc_dctx"()

    footer_t* _read_footer "read_footer"(void* input_map, long filesize)
    divisions_t* _read_divisions "read_divisions"(void* input_map, long position, int n_divisions)
    division_t* _flatten_divisions "flatten_divisions"(divisions_t* divisions)
    block_len_queue_t* _read_block_len_queue "read_block_len_queue"(void* input_map, long offset, long end)

    char* _extract_spectrum_mz "extract_spectrum_mz"(char* input_map, ZSTD_DCtx* dctx, data_format_t* df, block_len_queue_t* _mz_binary_block_lens, long mz_binary_blk_pos, divisions_t* divisions, long index, size_t* out_len, int encode)
    char* _extract_spectrum_inten "extract_spectrum_inten"(char* input_map, ZSTD_DCtx* dctx, data_format_t* df, block_len_queue_t* _inten_binary_block_lens, long inten_binary_blk_pos, divisions_t* divisions, long index, size_t* out_len, int encode)
    char* _extract_spectra "extract_spectra"(char* input_map, ZSTD_DCtx* dctx, data_format_t* df, block_len_queue_t* _xml_block_lens, block_len_queue_t* _mz_binary_block_lens, block_len_queue_t* _inten_binary_block_lens, long xml_pos, long mz_pos, long inten_pos, int mz_fmt, int inten_fmt, divisions_t* divisions, long index, size_t* out_len)
    void _compress_mzml "compress_mzml"(char* input_map, size_t input_filesize, Arguments* arguments, data_format_t* df, divisions_t* divisions, int output_fd)
    void _decompress_msz "decompress_msz"(char* input_map, size_t input_filesize, Arguments* arguments, int fd)
