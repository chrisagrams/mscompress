#ifndef MSCOMPRESS_H
#define MSCOMPRESS_H

#include <stdint.h>
#include <sys/types.h>

#include "../vendor/zlib/zlib.h"
#include "../vendor/zstd/lib/zstd.h"

/* Build status reported by the CLI (`--version`). The CLI's CMakeLists.txt
   defines this from the build configuration; the fallback covers consumers
   that compile these sources without it (Python setup.py, Node addon). */
#ifndef STATUS
#define STATUS "Release"
#endif
#define MIN_SUPPORT "0.1"
#define MAX_SUPPORT "0.2"
#define ADDRESS "chrisagrams@gmail.com"

/*
 * On-disk msz format stamps, written to header bytes 4-11.
 *
 * 0.1 predates any version check and is stored as (1, 0); 0.2 onward store the
 * version itself. A file is stamped 0.2 only when it uses a feature 0.1 cannot
 * express (currently the byte shuffle), so output a 0.1 reader could parse
 * stays labeled 0.1.
 */
#define MSZ_V01_MAJOR 1
#define MSZ_V01_MINOR 0
#define MSZ_V02_MAJOR 0
#define MSZ_V02_MINOR 2

#define FORMAT_VERSION_OFFSET 4

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

#define TARGET_MZ 0x01
#define TARGET_INT 0x02

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

/*
 * Byte-shuffle marker: a flag bit OR'd into the footer's mz_fmt/inten_fmt
 * rather than a new footer field, so sizeof(footer_t) - which read_footer()
 * uses to locate the footer from the end of the file - is unchanged and older
 * .msz files still parse. Accessions stop at 4700012, so bit 24 is free.
 *
 * Always unwrap a stored format with MSZ_ALGO() before dispatching on it.
 */
#define _shuffle_flag_ 0x01000000
#define MSZ_ALGO(fmt) ((fmt) & ~_shuffle_flag_)
#define MSZ_HAS_SHUFFLE(fmt) (((fmt) & _shuffle_flag_) != 0)

#define COMPRESS 1
#define DECOMPRESS 2
#define EXTRACT 3
#define EXTRACT_MSZ 4
#define EXTERNAL 5
#define DESCRIBE 6
#define DECOMPRESS_MSZX 7

#define MSLEVEL 0x01
#define SCANNUM 0x02
#define RETTIME 0x04

#ifdef __cplusplus
extern "C" {
#endif

extern int verbose;

typedef struct {
   const char* name;
   int type;
   int target;
   const char* description;
   float default_mz_scale;
   float default_int_scale;
   int experimental;
} algo_info_t;

typedef struct {
   int verbose;
   int threads;
   int extract_only;
   int describe_only;
   const char* mz_lossy;
   const char* int_lossy;
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

   /* Byte-shuffle binary streams before compressing. On by default; --no-shuffle
      turns it off. `shuffle_explicit` records whether the caller asked either
      way, so the "skipped for a lossy stream" warnings only fire for someone who
      actually requested it. */
   int shuffle;
   int shuffle_explicit;

   int json_output;
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

   // Per-spectrum raw payload lengths within `cache`, as parsed from the
   // ZLIB_SIZE_OFFSET-byte headers (lens[i]=0 for empty spectra that were
   // skipped during compression). Populated lazily by the Python no-encode
   // fast path (extract.c:extract_no_encode_from_cache) so subsequent
   // accesses skip the cache walk. Distinct from `encoded_cache_lens`, which
   // holds RE-encoded sizes from the slow path's `encode_binary_block`.
   size_t* cache_spec_lens;

   // Shared-LRU bookkeeping. Membership and ordering are maintained by
   // lru_touch_block(); these fields are unused when the block is not in any
   // LRU. A block belongs to at most one block_lru_t at a time. See
   // `block_lru_t` below.
   struct block_len_t* lru_prev;
   struct block_len_t* lru_next;
   int lru_pinned;  // 1 if currently in an LRU's list, 0 otherwise.

} block_len_t;

typedef struct {
   block_len_t* head;
   block_len_t* tail;

   int populated;
} block_len_queue_t;

/**
 * @brief Process-wide shared, byte-budgeted LRU of decompressed block caches.
 *
 * Caps the cumulative memory held in `block_len_t->cache` (and the associated
 * encoded_cache / *_lens buffers) across ALL block queues that touch it —
 * typically every open MSZFile's xml/mz/intensity queues share one instance,
 * so aggregate decompressed-cache memory obeys a single ceiling regardless of
 * how many files are open. Eviction frees the cache buffers on the evicted
 * block but never frees the `block_len_t` node itself (owned by its queue).
 *
 * Budget is in BYTES (`blk->original_size` per cached block), not block count,
 * so it behaves correctly across files with very different block sizes.
 *
 * `lock` is an opaque pointer to a platform mutex (allocated in queue.c) so the
 * heavy platform headers stay out of this widely-included header. All public
 * operations take the lock; pass NULL for `lru` to disable caching bounds.
 */
typedef struct {
   block_len_t* head;   // Most-recently-touched block.
   block_len_t* tail;   // Least-recently-touched block (eviction victim).
   size_t cur_bytes;    // Sum of original_size over currently-cached blocks.
   size_t cap_bytes;    // Maximum cur_bytes before eviction. 0 = unbounded.
   void* lock;          // Opaque platform mutex (see queue.c).
} block_lru_t;

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

/**
 * @brief Compression function type.
 * This function takes a `ZSTD_CCtx` pointer, input data, input size,
 * output size pointer, and compression level, and returns a pointer to the
 * compressed data.
 * @param czstd A pointer to a `ZSTD_CCtx` struct for compression.
 * @param src A pointer to the input data to be compressed.
 * @param src_size The size of the input data.
 * @param dest_len A pointer to a `size_t` where the size of the compressed data
 * will be stored.
 * @param compression_level The compression level to use.
 * @return A pointer to the compressed data. Returns NULL on error.
 */
typedef void* (*compression_fun)(ZSTD_CCtx*, void*, size_t, size_t*, int);
typedef compression_fun (*compression_fun_ptr)();

/**
 * @brief Decompression function type.
 * This function takes a `ZSTD_DCtx` pointer, input data, input size,
 * output size pointer, and returns a pointer to the decompressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param src A pointer to the input data to be decompressed.
 * @param src_size The size of the input data.
 * @param dest_len A pointer to a `size_t` where the size of the decompressed
 * data will be stored.
 * @return A pointer to the decompressed data. Returns NULL on error.
 */
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

   /* Byte-shuffle element width per binary stream, in bytes. 0 disables it.
    * Derived from the source data type on compress, and from the footer's
    * shuffle flag plus the source data type on decompress. */
   int mz_shuffle_elem;
   int inten_shuffle_elem;

} data_format_t;

/* arguments.c */
void init_args(Arguments* args);
int set_threads(Arguments* args, int threads);
int set_mz_lossy(Arguments* args, const char* mz_lossy);
int set_int_lossy(Arguments* args, const char* int_lossy);
int set_mz_scale_factor(Arguments* args, const char* scale_factor_str);
int set_int_scale_factor(Arguments* args, const char* scale_factor_str);
int validate_args(Arguments* args, char* err_buf, size_t err_buf_size);
void print_algorithms(void);
void print_algorithms_json(void);
int set_compress_runtime_variables(Arguments* args, data_format_t* df);
int set_decompress_runtime_variables(data_format_t* df, footer_t* msz_footer);

/* file.c */

void* get_mapping(int fd);
void* get_mapping_range(int fd, int64_t offset, size_t length,
                        size_t* out_pad, size_t* out_map_length);
int find_tar_entry(int fd, const char* name, int64_t* out_offset,
                   size_t* out_size);
int walk_tar_entries(int fd,
                     int (*cb)(const char* name, int64_t data_offset,
                               size_t data_size, void* user),
                     void* user);
int remove_mapping(void* addr, size_t length);
int flush(int fd);
int remove_file(char* path);
size_t get_filesize(char* path);
size_t write_to_file(int fd, char* buff, size_t n);
size_t read_from_file(int fd, void* buff, size_t n);
size_t write_header(int fd, data_format_t* df, long blocksize, char* md5);
long get_offset(int fd);
long get_header_blocksize(void* input_map);
data_format_t* get_header_df(void* input_map);
size_t write_footer(footer_t* footer, int fd);
footer_t* read_footer(void* input_map, long filesize);
void print_footer_csv(footer_t* footer, int msz_major, int msz_minor);
void print_footer_json(footer_t* footer, int msz_major, int msz_minor);
int prepare_fds(char* input_path, char** output_path, char* debug_output,
                char** input_map, long* input_filesize, int* fds);
int determine_filetype(void* input_map, size_t input_length);
char* change_extension(char* input, char* extension);
int open_input_file(char* input_path);
int open_output_file(char* path);
int is_mzml(void* input_map, size_t input_length);
int is_msz(void* input_map, size_t input_length);
const char* msz_version_string(int major, int minor);
int msz_read_version(void* input_map, long filesize, int* major, int* minor);
int msz_require_version(void* input_map, long filesize);
int close_file(int fd);

/* mszx.c */

typedef struct {
   char* name;     /* heap-allocated, NUL-terminated entry name */
   int64_t offset; /* data byte offset in the archive */
   size_t size;    /* logical size from the tar header */
} mszx_entry_t;

int mszx_list_entries(int fd, mszx_entry_t** out, size_t* out_count);
void mszx_free_entries(mszx_entry_t* entries, size_t count);
int decompress_mszx(char* input_path, char* output_dir, Arguments* arguments);

/* mem.c */

data_block_t* alloc_data_block(size_t max_size);
data_block_t* realloc_data_block(data_block_t* db, size_t new_size);
int dealloc_data_block(data_block_t* db);
cmp_block_t* alloc_cmp_block(char* mem, size_t size, size_t original_size);
int dealloc_cmp_block(cmp_block_t* blk);

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
void dealloc_division(division_t* div);
void dealloc_divisions(divisions_t* divisions);
void dealloc_read_division(division_t* div);
void dealloc_read_divisions(divisions_t* divisions);
size_t write_divisions(divisions_t* divisions, int fd);
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
int parse_footer(footer_t** footer, void* input_map, long input_filesize,
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

/* Callback types for error/warning handling */
typedef void (*error_callback_t)(const char* message);
typedef void (*warning_callback_t)(const char* message);

/* Set custom error/warning callbacks (for Python bindings) */
void set_error_callback(error_callback_t callback);
void set_warning_callback(warning_callback_t callback);
void reset_error_callback(void);
void reset_warning_callback(void);

/* decode.c */

void decode_base64(char* src, char* dest, size_t src_len, size_t* out_len);
decode_fun set_decode_fun(int compression_method, int algo, int accession);
// Bytef* decode_binary(char* input_map, int start_position, int end_position,
// int compression_method, size_t* out_len);

/* encode.c */

encode_fun set_encode_fun(int compression_method, int algo, int accession);
void no_encode_no_header(z_stream* z, char** src, size_t src_len, char* dest,
                         size_t* out_len);
void encode_base64(zlib_block_t* zblk, char* dest, size_t src_len,
                   size_t* out_len);

// char* encode_binary(char** src, int compression_method, size_t* out_len);

/* extract.c */
void extract_mzml(char* input_map, divisions_t* divisions, int output_fd);
void extract_msz(char* input_map, size_t input_filesize, long* indicies,
                 long indicies_length, uint32_t* scans, long scans_length,
                 uint16_t ms_level, int output_fd);

void extract_mzml_filtered(char* input_map, size_t input_filesize,
                           long* indicies, long indicies_length,
                           uint32_t* scans, long scans_length,
                           uint16_t ms_level, division_t* division,
                           int output_fd);

char* extract_spectrum_mz(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                          block_len_queue_t* mz_binary_block_lens,
                          long mz_binary_blk_pos, divisions_t* divisions,
                          long index, size_t* out_len, int encode,
                          block_lru_t* lru);

char* extract_spectrum_inten(char* input_map, ZSTD_DCtx* dctx,
                             data_format_t* df,
                             block_len_queue_t* inten_binary_block_lens,
                             long inten_binary_blk_pos, divisions_t* divisions,
                             long index, size_t* out_len, int encode,
                             block_lru_t* lru);

char* extract_spectra(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                      block_len_queue_t* xml_block_lens,
                      block_len_queue_t* mz_binary_block_lens,
                      block_len_queue_t* inten_binary_block_lens, long xml_pos,
                      long mz_pos, long inten_pos, int mz_fmt, int inten_fmt,
                      divisions_t* divisions, long index, size_t* out_len,
                      block_lru_t* lru);

char* extract_mzml_header(char* blk, division_t* first_division,
                          size_t* out_len);

char* extract_mzml_footer(char* blk, divisions_t* divisions, size_t* out_len);

char* update_spectrum_list_count(char* header, size_t header_len,
                                 long new_count, size_t* out_len);

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
size_t dump_block_len_queue(block_len_queue_t* queue, int fd);
int compress_mzml(char* input_map, size_t input_filesize, Arguments* arguments,
                  data_format_t* df, divisions_t* divisions, int output_fd);
int get_compress_type(char* arg);
compression_fun set_compress_fun(int accession);

/* decompress.c */

/**
 * @brief Structure containing the arguments for decompression.
 * @param input_map The input buffer containing the compressed data.
 * @param df A pointer to a data_format_t struct containing the data format
 * information.
 * @param xml_blk A pointer to a block_len_t struct containing the original and
 * compressed sizes
 * @param mz_binary_blk A pointer to a block_len_t struct containing the
 * original and compressed sizes of the m/z binary block.
 * @param inten_binary_blk A pointer to a block_len_t struct containing the
 * original and compressed
 * @param division A pointer to a division_t struct containing the division
 * information.
 * @param footer_xml_off The offset within the input buffer where the XML block
 * starts.
 * @param footer_mz_bin_off The offset within the input buffer where the m/z
 * binary block starts.
 * @param footer_inten_bin_off The offset within the input buffer where the
 * intensity binary block starts.
 * @param ret A pointer to a char* where the decompressed data will be stored.
 * @param ret_len A pointer to a size_t where the length of the decompressed
 * data will be stored.
 */
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
int decompress_msz(char* input_map, size_t input_filesize, Arguments* args,
                   int fd);
decompression_fun set_decompress_fun(int accession);

/* algo.c */
/**
 * @brief Structure containing the arguments for the algorithm.
 * @param src A pointer to a `char*` where the source data is stored.
 * @param src_len The length of the source data.
 * @param dest A pointer to a `char*` where the destination data will be stored.
 * @param dest_len A pointer to a `size_t` where the length of the destination
 * data will be stored.
 * @param src_format An integer representing the format of the source data.
 * @param enc_fun A function pointer to the encoding function to be used.
 * @param dec_fun A function pointer to the decoding function to be used.
 * @param tmp A pointer to a `data_block_t` struct used for temporary storage
 * during encoding/decoding.
 * @param z A pointer to a deflate-initialized `z_stream` struct used for zlib
 * compression (`enc_fun` paths).
 * @param z_inflate A pointer to an inflate-initialized `z_stream` struct used
 * for zlib decompression (`dec_fun` paths). Kept separate from `z` so the
 * compress pipeline can hold both live at once and each stream keeps a
 * balanced init-once/reset-per-block/end-once lifecycle.
 * @param scale_factor A float representing the scale factor to be applied to
 * the data during encoding/decoding.
 * @param ret_code An integer representing the return code of the algorithm.
 */
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
   z_stream* z_inflate;
   float scale_factor;
   int ret_code;
   Algo algo_fun;
   int shuffle_elem; /* Byte-shuffle element width for this stream; 0 = off. */
} algo_args;

extern const algo_info_t algo_registry[];
extern const int algo_registry_size;
Algo set_compress_algo(int algo, int accession);
Algo set_decompress_algo(int algo, int accession);
int get_algo_type(const char* arg);

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

/* Shared, byte-budgeted LRU over `block_len_t` decompressed-block caches. */
block_lru_t* alloc_block_lru(size_t cap_bytes);
void dealloc_block_lru(block_lru_t* lru);
void lru_touch_block(block_lru_t* lru, block_len_t* blk);
void lru_remove_queue_blocks(block_lru_t* lru, block_len_queue_t* queue);
void lru_evict_all(block_lru_t* lru);
size_t block_cache_usage(block_lru_t* lru);
size_t block_cache_cap(block_lru_t* lru);
void block_drop_cache(block_len_t* blk);

block_len_t* pop_block_len(block_len_queue_t* queue);
size_t dump_block_len_queue(block_len_queue_t* queue, int fd);
block_len_queue_t* read_block_len_queue(void* input_map, long offset, long end);

/* zl.c */

zlib_block_t* zlib_alloc(int offset);
z_stream* alloc_z_stream();
void dealloc_z_stream(z_stream* z);
z_stream* alloc_z_stream_inflate();
void dealloc_z_stream_inflate(z_stream* z);
int zlib_realloc(zlib_block_t* old_block, size_t new_size);
void zlib_dealloc(zlib_block_t* blk);
int zlib_append_header(zlib_block_t* blk, void* content, size_t size);
void* zlib_pop_header(zlib_block_t* blk);
uInt zlib_compress(z_stream* z, Bytef* input, zlib_block_t* output,
                   uInt input_len);
uInt zlib_decompress(z_stream* z, Bytef* input, zlib_block_t* output,
                     uInt input_len);

#ifdef __cplusplus
}
#endif

#endif /* MSCOMPRESS_H */
