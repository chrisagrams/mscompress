#include <argp.h>
#include <zstd.h>
#include <sys/types.h>

#define VERSION "MSCompress 0.0.1"
#define STATUS "Dev"
#define ADDRESS "chrisagrams@gmail.com"

#define FORMAT_VERSION_MAJOR 1
#define FORMAT_VERSION_MINOR 0

#define BUFSIZE 4096

#define MAGIC_TAG 0x035F51B5
#define MESSAGE "MS Compress Format 1.0 Gao Laboratory at UIC"

struct arguments
{
  char *args[2];            /* ARG1 and ARG2 */
  int verbose;              /* The -v flag */
  int threads;
  int blocksize;
  int checksum; 
};

static error_t parse_opt (int key, char *arg, struct argp_state *state);

typedef struct
{
    int mz_fmt;
    int int_fmt;
    int compression;
    int total_spec;
    int populated;
} data_format_t;

typedef struct
{
    off_t* start_positions;
    off_t* end_positions;
    off_t* positions_len;
    int total_spec;
    size_t file_end;
} data_positions_t;


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
    size_t compresssed_size;
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
    off_t xml_blk_pos;
    off_t binary_blk_pos;
    off_t dp_pos;
} footer_t;





#define _32i_ 1000519
#define _16e_ 1000520
#define _32f_ 1000521
#define _64i_ 1000522
#define _64d_ 1000523

#define _zlib_ 1000574
#define _no_comp_ 1000576

#define _intensity_ 1000515
#define _mass_ 1000514


#define COMPRESS 1
#define DECOMPRESS 2


/* file.c */
void* get_mapping(int fd);
int remove_mapping(void* addr, int fd);
int get_blksize(char* path);
size_t get_filesize(char* path);
ssize_t write_to_file(int fd, char* buff, size_t n);
ssize_t read_from_file(int fd, void* buff, size_t n);
void write_header(int fd, char* xml_compression_method, char* binary_compression_method, char* md5);
off_t get_offset(int fd);
void write_footer(footer_t footer, int fd);
footer_t* read_footer(int fd);

int is_mzml(int fd);
int is_msz(int fd);

/* preproccess.c */
data_format_t* pattern_detect(char* input_map);
data_positions_t*find_binary(char* input_map, data_format_t* df);
void get_encoded_lengths(char* input_map, data_positions_t* dp);
data_positions_t** get_binary_divisions(data_positions_t* dp, long* blocksize, int* divisions, int threads);
data_positions_t** get_xml_divisions(data_positions_t* dp, data_positions_t** binary_divisions, int divisions);
void free_ddp(data_positions_t** ddp, int divisions);
void dump_dp(data_positions_t* dp, int fd);
data_positions_t* read_dp(void* input_map, long dp_position, long eof);

/* sys.c */
int get_cpu_count();
int get_thread_id();


/* decode.c */
double* decode_binary(char* input_map, int start_position, int end_position, int compression_method, size_t* out_len);

/* compress.c */
typedef struct {
    char* input_map;
    data_positions_t* dp;
    data_format_t* df;
    size_t cmp_blk_size;

    cmp_blk_queue_t* ret;
} compress_args_t;
    
ZSTD_CCtx* alloc_cctx();
void * zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level);
void compress_routine(void* args);
block_len_queue_t* compress_parallel(char* input_map, data_positions_t** ddp, data_format_t* df, size_t cmp_blk_size, int divisions, int fd);
void dump_block_len_queue(block_len_queue_t* queue, int fd); 

/* decompress.c */
ZSTD_DCtx* alloc_dctx();
void * zstd_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len, size_t org_len);

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
void dump_block_len_queue(block_len_queue_t* queue, int fd);
block_len_queue_t* read_block_len_queue(void* input_map, int offset, int end);