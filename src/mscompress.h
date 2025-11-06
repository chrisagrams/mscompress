#ifndef MSCOMPRESS_H
#define MSCOMPRESS_H

#include <stdint.h>
#include <sys/types.h>

#include "../vendor/zlib/zlib.h"
#include "../vendor/zstd/lib/zstd.h"

#define VERSION "1.0.1"
#define STATUS "Dev"
#define MIN_SUPPORT "0.1"
#define MAX_SUPPORT "0.1"
#define ADDRESS "chrisagrams@gmail.com"

#define FORMAT_VERSION_MAJOR 1
#define FORMAT_VERSION_MINOR 0

#define BUFSIZE 4096
#define ZLIB_BUFF_FACTOR 1024000  // initial size of zlib buffer

#define ZLIB_TYPE uint32_t  // type and size of header used for encode/decode
#define ZLIB_SIZE_OFFSET sizeof(ZLIB_TYPE)

#define REALLOC_FACTOR 1.1  // realloc factor for zlib buffer

#define MAGIC_TAG 0x035F51B5
#define MESSAGE "MS Compress Format 1.0 Gao Laboratory at UIC"

#define MESSAGE_SIZE 128
#define MESSAGE_OFFSET 12
#define DATA_FORMAT_T_OFFSET 140
#define DATA_FORMAT_T_SIZE 36 /* ignores private members (serialized)*/
#define BLOCKSIZE_OFFSET 176
#define MD5_OFFSET 184
#define MD5_SIZE 32
#define HEADER_SIZE 512

#define DEBUG 0

#define TRUE 1
#define FALSE 0

#define _32i_ 1000519
#define _16e_ 1000520
#define _32f_ 1000521
#define _64i_ 1000522
#define _64d_ 1000523

#define _zlib_ 1000574
#define _no_comp_ 1000576

#define _intensity_ 1000515
#define _mass_ 1000514
#define _xml_ 1000513  // TODO: change this

#define _lossless_ 4700000
#define _ZSTD_compression_ 4700001
#define _cast_64_to_32_ 4700002
#define _log2_transform_ 4700003
#define _delta16_transform_ 4700004
#define _delta24_transform_ 4700005
#define _delta32_transform_ 4700006
#define _vbr_ 4700007
#define _bitpack_ 4700008
#define _vdelta16_transform_ 4700009  // TODO fix these
#define _vdelta24_transform_ 4700010
#define _cast_64_to_16_ 4700011
#define _no_encode_ 4700012

#define _LZ4_compression_ 4700012

#define ERROR_CHECK 1 /* If defined, runtime error checks will be enabled. */

#define COMPRESS 1
#define DECOMPRESS 2
#define EXTRACT 3
#define EXTRACT_MSZ 4
#define EXTERNAL 5
#define DESCRIBE 6

#define MSLEVEL 0x01
#define SCANNUM 0x02
#define RETTIME 0x04

#ifdef __cplusplus
extern "C" {
#endif

extern int verbose;

typedef struct {
   int verbose;
   int threads;
   int extract_only;
   int describe_only;
   char* mz_lossy;
   char* int_lossy;
   long blocksize;
   char* input_file;
   char* output_file;
   float mz_scale_factor;
   float int_scale_factor;
   long* indices;
   long indices_length;
   uint32_t* scans;
   long scans_length;
   long ms_level;

   int target_xml_format;
   int target_mz_format;
   int target_inten_format;

   int zstd_compression_level;
} Arguments;

typedef struct {
   uint64_t* start_positions;
   uint64_t* end_positions;
   int total_spec;
   size_t file_end;  // TODO: remove this
} data_positions_t;

typedef struct {
   data_positions_t* spectra;
   data_positions_t* xml;
   data_positions_t* mz;
   data_positions_t* inten;

   uint64_t size;

   uint32_t* scans;
   uint16_t* ms_levels;
   float* ret_times;

} division_t;

typedef struct {
   division_t** divisions;
   int n_divisions;

} divisions_t;

typedef struct {
   char* mem;
   size_t size;
   size_t max_size;
} data_block_t;

typedef struct cmp_block_t {
   char* mem;
   size_t size;
   size_t original_size;
   size_t max_size;

   struct cmp_block_t* next;
} cmp_block_t;

typedef struct {
   cmp_block_t* head;
   cmp_block_t* tail;

   int populated;
} cmp_blk_queue_t;

typedef struct block_len_t {
   size_t original_size;
   size_t compressed_size;
   struct block_len_t* next;

   char* cache;  // During msz extraction, store decompressed block here as a
                 // "cache".

   char* encoded_cache;
   uint32_t encoded_cache_fmt;
   uint64_t encoded_cache_len;
   size_t* encoded_cache_lens;

} block_len_t;

typedef struct {
   block_len_t* head;
   block_len_t* tail;

   int populated;
} block_len_queue_t;

typedef struct {
   uint64_t xml_pos;  // msz file position of start of compressed XML data.
   uint64_t mz_binary_pos;     // msz file position of start of compressed m/z
                               // binary data.
   uint64_t inten_binary_pos;  // msz file position of start of compressed int
                               // binary data.
   uint64_t xml_blk_pos;
   uint64_t mz_binary_blk_pos;
   uint64_t inten_binary_blk_pos;
   uint64_t divisions_t_pos;
   size_t num_spectra;
   uint64_t original_filesize;
   int n_divisions;
   int magic_tag;
   int mz_fmt;
   int inten_fmt;
} footer_t;

typedef struct {
   Bytef* mem;
   Bytef* buff;
   size_t size;
   size_t len;
   size_t offset;

} zlib_block_t;

typedef void (*Algo)(void*);
typedef Algo (*Algo_ptr)();

typedef void (*decode_fun)(z_stream*, char*, size_t, char**, size_t*,
                           data_block_t*);
typedef decode_fun (*decode_fun_ptr)();

typedef void (*encode_fun)(z_stream*, char**, size_t, char*, size_t*);
typedef encode_fun (*encode_fun_ptr)();

typedef void* (*compression_fun)(ZSTD_CCtx*, void*, size_t, size_t*, int);
typedef compression_fun (*compression_fun_ptr)();

typedef void* (*decompression_fun)(ZSTD_DCtx*, void*, size_t, size_t);
typedef decompression_fun (*decompression_fun_ptr)();

typedef struct {
   /* source information (source mzML) */
   uint32_t source_mz_fmt;
   uint32_t source_inten_fmt;
   uint32_t source_compression;
   uint32_t source_total_spec;

   /* target information (target msz)*/
   uint32_t target_xml_format;
   uint32_t target_mz_format;
   uint32_t target_inten_format;

   /* algo parameters */
   float mz_scale_factor;
   float int_scale_factor;

   /* runtime variables, not written to disk. */
   int populated;
   decode_fun decode_source_compression_mz_fun;
   decode_fun decode_source_compression_inten_fun;
   encode_fun encode_source_compression_mz_fun;
   encode_fun encode_source_compression_inten_fun;
   Algo target_xml_fun;
   Algo target_mz_fun;
   Algo target_inten_fun;
   compression_fun xml_compression_fun;
   compression_fun mz_compression_fun;
   compression_fun inten_compression_fun;
   decompression_fun xml_decompression_fun;
   decompression_fun mz_decompression_fun;
   decompression_fun inten_decompression_fun;

   int zstd_compression_level;  // no need to write to file since ZSTD_DCtx
                                // doesn't need it.

} data_format_t;

/* arguments.c */
void init_args(Arguments* args);
int set_threads(Arguments* args, int threads);
int set_mz_lossy(Arguments* args, const char* mz_lossy);
int set_int_lossy(Arguments* args, const char* int_lossy);
int set_mz_scale_factor(Arguments* args, const char* scale_factor_str);
int set_int_scale_factor(Arguments* args, const char* scale_factor_str);
void set_compress_runtime_variables(Arguments* args, data_format_t* df);
void set_decompress_runtime_variables(data_format_t* df, footer_t* msz_footer);

/* file.c */
extern long fd_pos[3];
extern int fds[3];

void* get_mapping(int fd);
int remove_mapping(void* addr, size_t length);
int flush(int fd);
int remove_file(char* path);
size_t get_filesize(char* path);
size_t write_to_file(int fd, char* buff, size_t n);
size_t read_from_file(int fd, void* buff, size_t n);
void write_header(int fd, data_format_t* df, long blocksize, char* md5);
long get_offset(int fd);
long get_header_blocksize(void* input_map);
data_format_t* get_header_df(void* input_map);
void write_footer(footer_t* footer, int fd);
footer_t* read_footer(void* input_map, long filesize);
void print_footer_csv(footer_t* footer);
int prepare_fds(char* input_path, char** output_path, char* debug_output,
                char** input_map, long* input_filesize, int* fds);
int determine_filetype(void* input_map, size_t input_length);
char* change_extension(char* input, char* extension);
int open_input_file(char* input_path);
int open_output_file(char* path);
int is_mzml(void* input_map, size_t input_length);
int is_msz(void* input_map, size_t input_length);
int close_file(int fd);

/* mem.c */

data_block_t* alloc_data_block(size_t max_size);
data_block_t* realloc_data_block(data_block_t* db, size_t new_size);
void dealloc_data_block(data_block_t* db);
cmp_block_t* alloc_cmp_block(char* mem, size_t size, size_t original_size);
void dealloc_cmp_block(cmp_block_t* blk);

/* preprocess.c */

long* map_ms_level_to_index(uint16_t ms_level, division_t* div,
                            long index_offset, long* indices_length);
long* map_ms_level_to_index_from_divisions(uint16_t ms_level,
                                           divisions_t* divisions,
                                           long* indicies_length);
data_format_t* pattern_detect(char* input_map);
void get_encoded_lengths(char* input_map, data_positions_t* dp);
long encodedLength_sum(data_positions_t* dp);
data_positions_t** get_binary_divisions(data_positions_t* dp, long* blocksize,
                                        int* divisions, int* threads);
data_positions_t*** new_get_binary_divisions(data_positions_t** ddp,
                                             int ddp_len, long* blocksize,
                                             int* divisions, long* threads);
data_positions_t** get_xml_divisions(data_positions_t* dp,
                                     data_positions_t** binary_divisions,
                                     int divisions);
void free_ddp(data_positions_t** ddp, int divisions);
Algo map_fun(int fun_num);
// void write_dp(data_positions_t* dp, int fd);
// data_positions_t* read_dp(void* input_map, long dp_position, size_t
// num_spectra, long eof);
void dump_ddp(data_positions_t** ddp, int divisions, int fd);
data_positions_t** read_ddp(void* input_map, long position);
void dealloc_df(data_format_t* df);
void dealloc_dp(data_positions_t* dp);
void write_divisions(divisions_t* divisions, int fd);
divisions_t* read_divisions(void* input_map, long position, int n_divisions);
division_t* flatten_divisions(divisions_t* divisions);
divisions_t* create_divisions(division_t* div, long n_divisions);
long determine_n_divisions(long filesize, long blocksize);
long get_division_size_max(divisions_t* divisions);
data_positions_t** join_xml(divisions_t* divisions);
data_positions_t** join_mz(divisions_t* divisions);
data_positions_t** join_inten(divisions_t* divisions);
long* string_to_array(char* str, long* size);
long* map_scan_to_index(uint32_t* scans, long scans_length, division_t* div,
                        long index_offset, long* indices_length);
long* map_scans_to_index_from_divisions(uint32_t* scans, long scans_length,
                                        divisions_t* divisions,
                                        long* indicies_length);
division_t* scan_mzml(char* input_map, data_format_t* df, long end, int flags);
int preprocess_mzml(char* input_map, long input_filesize, long* blocksize,
                    Arguments* arguments, data_format_t** df,
                    divisions_t** divisions);
void parse_footer(footer_t** footer, void* input_map, long input_filesize,
                  block_len_queue_t** xml_block_lens,
                  block_len_queue_t** mz_binary_block_lens,
                  block_len_queue_t** inten_binary_block_lens,
                  divisions_t** divisions, int* n_divisions);
int preprocess_external(char* input_map, long input_filesize, long* blocksize,
                        Arguments* arguments, data_format_t** df,
                        divisions_t** divisions);
data_format_t* create_external_df();

/* sys.c */

int get_num_threads();
void prepare_threads(Arguments* args);
int get_thread_id();
double get_time(void);
int print(const char* format, ...);
int error(const char* format, ...);
int warning(const char* format, ...);
long parse_blocksize(char* arg);

/* decode.c */

void decode_base64(char* src, char* dest, size_t src_len, size_t* out_len);
decode_fun set_decode_fun(int compression_method, int algo, int accession);
// Bytef* decode_binary(char* input_map, int start_position, int end_position,
// int compression_method, size_t* out_len);

/* encode.c */

encode_fun set_encode_fun(int compression_method, int algo, int accession);
void encode_base64(zlib_block_t* zblk, char* dest, size_t src_len,
                   size_t* out_len);

// char* encode_binary(char** src, int compression_method, size_t* out_len);

/* extract.c */
void extract_mzml(char* input_map, divisions_t* divisions, int output_fd);
void extract_msz(char* input_map, size_t input_filesize, long* indicies,
                 long indicies_length, uint32_t* scans, long scans_length,
                 uint16_t ms_level, int output_fd);

char* extract_spectrum_mz(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                          block_len_queue_t* mz_binary_block_lens,
                          long mz_binary_blk_pos, divisions_t* divisions,
                          long index, size_t* out_len, int encode);

char* extract_spectrum_inten(char* input_map, ZSTD_DCtx* dctx,
                             data_format_t* df,
                             block_len_queue_t* inten_binary_block_lens,
                             long inten_binary_blk_pos, divisions_t* divisions,
                             long index, size_t* out_len, int encode);

char* extract_spectra(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                      block_len_queue_t* xml_block_lens,
                      block_len_queue_t* mz_binary_block_lens,
                      block_len_queue_t* inten_binary_block_lens, long xml_pos,
                      long mz_pos, long inten_pos, int mz_fmt, int inten_fmt,
                      divisions_t* divisions, long index, size_t* out_len);

/* compress.c */
typedef struct {
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
void* zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len,
                    size_t* out_len, int compression_level);
void* compress_routine(void* args);
void dump_block_len_queue(block_len_queue_t* queue, int fd);
void compress_mzml(char* input_map, size_t input_filesize, Arguments* arguments,
                   data_format_t* df, divisions_t* divisions, int output_fd);
int get_compress_type(char* arg);
compression_fun set_compress_fun(int accession);

/* decompress.c */
typedef struct {
   char* input_map;
   int binary_encoding;
   data_format_t* df;
   block_len_t* xml_blk;
   block_len_t* mz_binary_blk;
   block_len_t* inten_binary_blk;
   division_t* division;
   uint64_t footer_xml_off;
   uint64_t footer_mz_bin_off;
   uint64_t footer_inten_bin_off;

   char* ret;
   size_t ret_len;

} decompress_args_t;

ZSTD_DCtx* alloc_dctx();
void* zstd_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len,
                      size_t org_len);
void* decmp_block(decompression_fun decompress_fun, ZSTD_DCtx* dctx,
                  void* input_map, long offset, block_len_t* blk);
void* decompress_routine(void* args);
void decompress_msz(char* input_map, size_t input_filesize, Arguments* args,
                    int fd);
decompression_fun set_decompress_fun(int accession);

/* algo.c */
typedef struct {
   char** src;
   size_t src_len;
   char** dest;
   size_t* dest_len;
   int src_format;
   encode_fun enc_fun;
   decode_fun dec_fun;
   data_block_t* tmp;
   z_stream* z;
   float scale_factor;
} algo_args;

Algo set_compress_algo(int algo, int accession);
Algo set_decompress_algo(int algo, int accession);
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
void append_block_len(block_len_queue_t* queue, size_t original_size,
                      size_t compressed_size);
block_len_t* get_block_by_index(block_len_queue_t* queue, int index);
long get_block_offset_by_index(block_len_queue_t* queue, int index);
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
uInt zlib_compress(z_stream* z, Bytef* input, zlib_block_t* output,
                   uInt input_len);
uInt zlib_decompress(z_stream* z, Bytef* input, zlib_block_t* output,
                     uInt input_len);

/* debug.c */
void dump_divisions_to_file(data_positions_t** ddp, int divisions, int threads,
                            int fd);

#ifdef __cplusplus
}
#endif

#endif /* MSCOMPRESS_H */
