#include <zstd.h>
#include "vendor/zlib/zlib.h"
#include <sys/types.h>

#define VERSION "0.0.1"
#define STATUS "Dev"
#define MIN_SUPPORT "0.1"
#define MAX_SUPPORT "0.1"
#define ADDRESS "chrisagrams@gmail.com"

#define FORMAT_VERSION_MAJOR 1
#define FORMAT_VERSION_MINOR 0

#define BUFSIZE 4096
#define ZLIB_BUFF_FACTOR 1024000
// #define ZLIB_BUFF_FACTOR 512000
// #define ZLIB_BUFF_FACTOR 4096
#define ZLIB_SIZE_OFFSET sizeof(uint16_t)

#define REALLOC_FACTOR 1.1

// #define DELTA_SCALE_FACTOR 6553.5
// #define DELTA_SCALE_FACTOR 100
// #define DELTA_SCALE_FACTOR 1000
#define DELTA_SCALE_FACTOR 3276.75


#define MAGIC_TAG 0x035F51B5
#define MESSAGE "MS Compress Format 1.0 Gao Laboratory at UIC"

#define MESSAGE_SIZE 128
#define MESSAGE_OFFSET 12
#define DATA_FORMAT_T_OFFSET 140
#define DATA_FORMAT_T_SIZE 28   /* ignores private members */
#define BLOCKSIZE_OFFSET 168
#define MD5_OFFSET 176
#define MD5_SIZE 32
#define HEADER_SIZE 512

#define DEBUG 0

#define _32i_ 1000519
#define _16e_ 1000520
#define _32f_ 1000521
#define _64i_ 1000522
#define _64d_ 1000523

#define _zlib_ 1000574
#define _no_comp_ 1000576

#define _intensity_ 1000515
#define _mass_ 1000514
#define _xml_ 1000513 //TODO: change this

#define _lossless_          4700000
#define _ZSTD_compression_  4700001
#define _cast_64_to_32_     4700002
#define _log2_transform_    4700003
#define _delta16_transform_ 4700004
#define _delta32_transform_ 4700005

#define ERROR_CHECK 1       /* If defined, runtime error checks will be enabled. */

#define COMPRESS 1
#define DECOMPRESS 2

extern int verbose;

struct Arguments {
    int verbose;
    int threads;
    char* mz_lossy;
    char* int_lossy;
    long blocksize;
    char* input_file;
    char* output_file;
    float mz_scale_factor;
    float int_scale_factor;
    long* indices;
    long indices_length;
    long* scans;
    long scans_length;
    long ms_level;
};

typedef void (*Algo)(void*);
typedef Algo (*Algo_ptr)();

typedef void (*decode_fun)(char*, size_t, char**, size_t*);
typedef decode_fun (*decode_fun_ptr)();

typedef void (*encode_fun)(char**, char*, size_t*);
typedef encode_fun (*encode_fun_ptr)();

typedef void* (*compression_fun)(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level);
typedef compression_fun (*compression_fun_ptr)();

typedef void* (*decompression_fun)(ZSTD_DCtx* dctx, void* src_buff, size_t src_len, size_t* out_len);
typedef decompression_fun (*decompression_fun_ptr)();

typedef struct
{

    /* source information (source mzML) */
    int source_mz_fmt;
    int source_inten_fmt;
    int source_compression;
    int source_total_spec;

    /* target information */
    int target_xml_format;
    int target_mz_format;
    int target_inten_format;

    /* runtime variables, not written to disk. */
    int populated;
    decode_fun_ptr decode_source_compression_mz_fun;
    decode_fun_ptr  decode_source_compression_inten_fun;
    encode_fun_ptr encode_source_compression_mz_fun;
    encode_fun_ptr encode_source_compression_inten_fun;
    Algo_ptr target_xml_fun;
    Algo_ptr target_mz_fun;
    Algo_ptr target_inten_fun;


} data_format_t;


typedef struct
{
    off_t* start_positions;
    off_t* end_positions;
    int total_spec;
    size_t file_end; //TODO: remove this
} data_positions_t;


typedef struct
{
    data_positions_t* spectra;
    data_positions_t* xml;
    data_positions_t* mz;
    data_positions_t* inten;

    size_t size;

    long* scans;
    long* ms_levels;

} division_t;


typedef struct
{
    division_t** divisions;
    int n_divisions;

} divisions_t;


typedef struct 
{
    char* mem;
    size_t size;
    size_t max_size;
} data_block_t;


typedef struct
{
    char* mem;
    size_t size;
    size_t original_size;
    size_t max_size;

    struct cmp_block_t* next;
} cmp_block_t;


typedef struct 
{
    cmp_block_t* head;
    cmp_block_t* tail;

    int populated;
} cmp_blk_queue_t;


typedef struct 
{
    size_t original_size;
    size_t compressed_size;
    struct block_len_t* next;

} block_len_t;


typedef struct 
{
    block_len_t* head;
    block_len_t* tail;

    int populated;
} block_len_queue_t;


typedef struct
{
    off_t xml_pos;          // msz file position of start of compressed XML data.
    off_t mz_binary_pos;    // msz file position of start of compressed m/z binary data.
    off_t inten_binary_pos;   // msz file position of start of compressed int binary data.
    off_t xml_blk_pos;
    off_t mz_binary_blk_pos;
    off_t inten_binary_blk_pos;
    off_t divisions_t_pos;
    size_t num_spectra;
    off_t original_filesize;
    int n_divisions;
    int magic_tag;
    int mz_fmt;
    int inten_fmt;
} footer_t;


typedef struct
{
    Bytef* mem;
    Bytef* buff;
    size_t size;
    size_t len;
    size_t offset;

} zlib_block_t;


/* file.c */
void* get_mapping(int fd);
int remove_mapping(void* addr, int fd);
int get_blksize(char* path);
size_t get_filesize(char* path);
ssize_t write_to_file(int fd, char* buff, size_t n);
ssize_t read_from_file(int fd, void* buff, size_t n);
void write_header(int fd, data_format_t* df, long blocksize, char* md5);
off_t get_offset(int fd);
long get_header_blocksize(void* input_map);
data_format_t* get_header_df(void* input_map);
void write_footer(footer_t* footer, int fd);
footer_t* read_footer(void* input_map, long filesize);
int prepare_fds(char* input_path, char** output_path, char* debug_output, char** input_map, long* input_filesize, int* fds);

int is_mzml(int fd);
int is_msz(int fd);


/* mem.c */
data_block_t* alloc_data_block(size_t max_size);
data_block_t* realloc_data_block(data_block_t* db, size_t new_size);
void dealloc_data_block(data_block_t* db);
cmp_block_t* alloc_cmp_block(char* mem, size_t size, size_t original_size);
void dealloc_cmp_block(cmp_block_t* blk);

/* preprocess.c */
data_format_t* pattern_detect(char* input_map);
void get_encoded_lengths(char* input_map, data_positions_t* dp);
long encodedLength_sum(data_positions_t* dp);
data_positions_t** get_binary_divisions(data_positions_t* dp, long* blocksize, int* divisions, int* threads);
data_positions_t*** new_get_binary_divisions(data_positions_t** ddp, int ddp_len, long* blocksize, int* divisions, long* threads);
data_positions_t** get_xml_divisions(data_positions_t* dp, data_positions_t** binary_divisions, int divisions);
void free_ddp(data_positions_t** ddp, int divisions);
Algo map_fun(int fun_num);
// void write_dp(data_positions_t* dp, int fd);
// data_positions_t* read_dp(void* input_map, long dp_position, size_t num_spectra, long eof);
void dump_ddp(data_positions_t** ddp, int divisions, int fd);
data_positions_t** read_ddp(void* input_map, long position);
void dealloc_df(data_format_t* df);
void dealloc_dp(data_positions_t* dp);
void write_divisions(divisions_t* divisions, int fd);
divisions_t* read_divisions(void* input_map, long position, int n_divisions);
data_positions_t** join_xml(divisions_t* divisions);
data_positions_t** join_mz(divisions_t* divisions);
data_positions_t** join_inten(divisions_t* divisions);
long* string_to_array(char* str, long* size);
void map_scan_to_index(struct Arguments* arguments, division_t* div);
int preprocess_mzml(char* input_map, long  input_filesize, long* blocksize, long n_threads, struct Arguments* arguments, data_format_t** df, divisions_t** divisions);
void parse_footer(footer_t** footer, void* input_map, long input_filesize, block_len_queue_t**xml_block_lens, block_len_queue_t** mz_binary_block_lens, block_len_queue_t** inten_binary_block_lens, divisions_t** divisions, int* n_divisions);

/* sys.c */
void prepare_threads(long args_threads, long* n_threads);
int get_thread_id();
int print(const char* format, ...);
int error(const char* format, ...);
int warning(const char* format, ...);
long parse_blocksize(char* arg);

/* decode.c */
decode_fun_ptr set_decode_fun(int compression_method, int algo);
// Bytef* decode_binary(char* input_map, int start_position, int end_position, int compression_method, size_t* out_len);

/* encode.c */
encode_fun_ptr set_encode_fun(int compression_method, int algo);
void encode_base64(zlib_block_t* zblk, char* dest, size_t src_len, size_t* out_len);
// char* encode_binary(char** src, int compression_method, size_t* out_len);

/* compress.c */
typedef struct 
{
    char* input_map;
    data_positions_t* dp;
    data_format_t* df;
    size_t cmp_blk_size;
    long blocksize;
    int mode;

    cmp_blk_queue_t* ret;
    compression_fun comp_fun;
    
} compress_args_t;
    
ZSTD_CCtx* alloc_cctx();
void * zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level);
void compress_routine(void* args);
block_len_queue_t* compress_parallel(char* input_map, data_positions_t** ddp, data_format_t* df, size_t cmp_blk_size, long blocksize, int mode, int divisions, int threads, int fd);
void dump_block_len_queue(block_len_queue_t* queue, int fd); 
void compress_mzml(char* input_map, long blocksize, int threads, footer_t* footer, data_format_t* df, divisions_t* divisions, int output_fd);

/* decompress.c */
typedef struct
{
    char* input_map;
    int binary_encoding;
    data_format_t* df;
    block_len_t* xml_blk;
    block_len_t* mz_binary_blk;
    block_len_t* inten_binary_blk;
    division_t* division;
    off_t footer_xml_off;
    off_t footer_mz_bin_off;
    off_t footer_inten_bin_off;

    char* ret;
    size_t ret_len;


} decompress_args_t;

ZSTD_DCtx* alloc_dctx();
void * zstd_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len, size_t org_len);
void decompress_routine(void* args);
void decompress_parallel(char* input_map,
                    block_len_queue_t* xml_block_lens,
                    block_len_queue_t* mz_binary_block_lens,
                    block_len_queue_t* inten_binary_block_lens,
                    divisions_t* divisions,
                    data_format_t* df,
                    footer_t* msz_footer,
                    int threads, int fd);


/* algo.c */
typedef struct
{
    char** src;
    size_t src_len;
    char** dest;
    size_t* dest_len;
    int src_format;
    encode_fun_ptr enc_fun;
    decode_fun_ptr dec_fun;
    data_block_t* tmp;
    z_stream* z;
} algo_args;

Algo_ptr set_compress_algo(int algo, int accession);
Algo_ptr set_decompress_algo(int algo, int accession);
int get_algo_type(char* arg);

/* queue.c */
cmp_blk_queue_t* alloc_cmp_buff();
void dealloc_cmp_buff(cmp_blk_queue_t* queue);
void append_cmp_block(cmp_blk_queue_t* queue, cmp_block_t* blk);
cmp_block_t* pop_cmp_block(cmp_blk_queue_t* queue);
block_len_t* alloc_block_len(size_t original_size, size_t compressed_size);
void dealloc_block_len(block_len_t* blk);
block_len_queue_t* alloc_block_len_queue();
void dealloc_block_len_queue(block_len_queue_t* queue);
void append_block_len(block_len_queue_t* queue, size_t original_size, size_t compressed_size);
block_len_t* pop_block_len(block_len_queue_t* queue);
void dump_block_len_queue(block_len_queue_t* queue, int fd);
block_len_queue_t* read_block_len_queue(void* input_map, long offset, long end);

/* zl.c */

zlib_block_t* zlib_alloc(int offset);
z_stream* alloc_z_stream();
void dealloc_z_stream(z_stream* z);
void zlib_realloc(zlib_block_t* old_block, size_t new_size);
void zlib_dealloc(zlib_block_t* blk);
int zlib_append_header(zlib_block_t* blk, void* content, size_t size);
void* zlib_pop_header(zlib_block_t* blk);
uInt zlib_compress(z_stream* z, Bytef* input, zlib_block_t* output, uInt input_len);
uInt zlib_decompress(z_stream* z, Bytef* input, zlib_block_t* output, uInt input_len);


/* debug.c */
void dump_divisions_to_file(data_positions_t** ddp, int divisions, int threads, int fd);