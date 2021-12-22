#include <argp.h>
#include <zstd.h>

#define VERSION "0.0.1"
#define STATUS "Development"
#define ADDRESS "chrisagrams@gmail.com"

#define FORMAT_VERSION_MAJOR 1
#define FORMAT_VERSION_MINOR 0

#define BUFSIZE 4096

#define MAGIC_TAG 0x035F51B5
#define MESSAGE "MS Compress Format 1.0 Gao Laboratory at UIC"

struct arguments
{
  char *args[0];            /* ARG1 and ARG2 */
  char *in_file;            /* Argument for -i */
  char *out_file;           /* Argument for -o */
  int version;              /* The -v flag */
};

static error_t parse_opt (int key, char *arg, struct argp_state *state);

typedef struct 
{
    int magic_tag;
    char* message;
    int mzml_file_pos[2];
    int* mass_file_pos;
    int* int_file_pos;
    char* compression_method;
    char* md5; 
    char reserved[84];
} file_header;

typedef struct df
{
    int mz_fmt;
    int int_fmt;
    int compression;
    int total_spec;
    int populated;
} data_format;

typedef struct dp
{
    int* start_positions;
    int* end_positions;
    int* encoded_lengths;
    int total_spec;
} data_positions;


#define _32i_ 1000519
#define _16e_ 1000520
#define _32f_ 1000521
#define _64i_ 1000522
#define _64d_ 1000523

#define _zlib_ 1000574
#define _no_comp_ 1000576

#define _intensity_ 1000515
#define _mass_ 1000514


// file_header file_header_default = {
//     .magic_tag = 0x035F51B5,
//     .message = "MS Compress Format 1.0 Gao Laboratory at UIC",
//     .mzml_file_pos = [300,0],
//      [0,0], [0,0], "ZSTD", "619D27BD267E835F6ED48B1F7FCBE90A"
// };



/* file.c */
void* get_mapping(int fd);
int remove_mapping(void* addr, int fd);
int get_blksize(char* path);
int write_to_file(int fd, char* buff, size_t n);
void write_header(int fd, char* compression_method, char* md5);

/* preproccess.c */
data_format* pattern_detect(char* input_map);
data_positions*find_binary(char* input_map, data_format* df);
void get_encoded_lengths(char* input_map, data_positions* dp);
long get_cpu_count();

/* decode.c */
double* decode_binary(char* input_map, int start_position, int end_position, int compression_method, size_t* out_len);

/* compress.c */
ZSTD_CCtx* alloc_cctx();
void * zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level);
void * compress_xml(char* input_map, data_positions* dp);

/* decompress.c */
ZSTD_DCtx* alloc_dctx();
void * zstd_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len, size_t org_len);