#include "mscompress.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <zstd.h>
#include <errno.h>

#include "vendor/base64/include/libbase64.h"


#include <stdbool.h>
#include "vendor/yxml/yxml.h"

int verbose = 0;

const char *argp_program_version = VERSION " " STATUS;
const char *argp_program_bug_address = ADDRESS;
static char doc[] = MESSAGE "\n" "MSCompress is used to compress mass spec raw data with high efficiency.";
static struct argp_option options[] = { 
    { "verbose", 'v', 0, OPTION_ARG_OPTIONAL, "Run in verbose mode."},
    { "threads", 't', "num", OPTION_ARG_OPTIONAL, "Set amount of threads to use. (default: auto)"},
    { "blocksize", 'b', "size", OPTION_ARG_OPTIONAL, "Set maximum blocksize (xKB, xMB, xGB). (default: 10MB)"},
    { "checksum", 'c', 0, OPTION_ARG_OPTIONAL, "Enable checksum generation. (disabled by default)"},
    { 0 } 
};

long
parse_blocksize(char* arg)
{
  int num;
  int len;
  char prefix[2];
  long res = -1;

  len = strlen(arg);
  num = atoi(arg);
  
  memcpy(prefix, arg+len-2, 2);

  if(strcmp(prefix, "KB") || strcmp(prefix, "kb"))
    res = num*1e+3;
  else if(strcmp(prefix, "MB") || strcmp(prefix, "mb"))
    res = num*1e+6;
  else if(strcmp(prefix, "GB") || strcmp(prefix, "gb"))
    res = num*1e+9;

  return res;
}

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;
  long blksize;
  switch (key)
    {
    case 'v':
      arguments->verbose = 1;
      break;
    case 't':
      arguments->threads = atoi(arg);
      if(arguments->threads <= 0)
        argp_error(state, "Number of threads cannot be less than 1.");
      break;
    case 'b':
      blksize = parse_blocksize(arg);
      if(blksize == -1)
        argp_error(state, "Unkown size suffix. (KB, MB, GB)");
      arguments->blocksize = blksize;
      break;
    case ARGP_KEY_ARG:
      if (state->arg_num >= 3)
      {
        argp_usage(state);
      }
      arguments->args[state->arg_num] = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 1)
      {
        argp_usage (state);
      }
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static char args_doc[] = "input_file (optional)output_file";

static struct argp argp = {options, parse_opt, args_doc, doc};


void prepare_threads(struct arguments args, long* n_threads)
{
    long cpu_count;

    cpu_count = get_cpu_count();

    if(args.threads == 0)
      *n_threads = cpu_count;
    else
      *n_threads = args.threads;
    
    print("\tUsing %d threads.\n", *n_threads);
}

void
get_compression_scheme(void* input_map, char** xml_compression_type, char** binary_compression_type)
{      
  *xml_compression_type = get_header_xml_compresssion_type(input_map);
  *binary_compression_type = get_header_binary_compression_type(input_map);

  print("\tXML compression scheme: %.*s\n", XML_COMPRESSION_METHOD_SIZE, *xml_compression_type);
  print("\tBinary compression scheme: %.*s\n", BINARY_COMPRESSION_METHOD_SIZE, *binary_compression_type);
}

void
parse_footer(footer_t** footer, void* input_map, long input_filesize,
            block_len_queue_t** xml_blks, block_len_queue_t** binary_blks, data_positions_t** dp)
{

      *footer = read_footer(input_map, input_filesize);

      print("\tXML position: %ld\n", (*footer)->xml_pos);
      print("\tBinary position: %ld\n", (*footer)->binary_pos);
      print("\tXML blocks position: %ld\n", (*footer)->xml_blk_pos);
      print("\tBinary blocks position: %ld\n", (*footer)->binary_blk_pos);
      print("\tdp block position: %ld\n", (*footer)->dp_pos);
      print("\tEOF position: %ld\n", input_filesize);
      print("\tOriginal file end: %ld\n", (*footer)->file_end);

      *xml_blks = read_block_len_queue(input_map, (*footer)->xml_blk_pos, (*footer)->binary_blk_pos);
      *binary_blks = read_block_len_queue(input_map, (*footer)->binary_blk_pos, (*footer)->dp_pos);
      *dp = read_dp(input_map, (*footer)->dp_pos, (*footer)->num_spectra, input_filesize);
      (*dp) -> file_end = (*footer)->file_end;
}

int 
main(int argc, char* argv[]) 
{
    struct arguments args;

    footer_t footer;
    struct timeval abs_start, abs_stop;
    long blocksize = 0;
    struct base64_state state;
    int divisions = 0;

    data_positions_t* dp = NULL;
    data_positions_t** binary_divisions = NULL;
    data_positions_t** xml_divisions = NULL;
    data_format_t* df = NULL;


    int fds[3] = {-1, -1, -1};
    void* input_map = NULL;
    size_t input_filesize = 0;
    long n_threads = 0;
    int operation = -1;

    args.verbose = 0;
    args.blocksize = 1e+10;
    args.threads = 0;
    args.args[0] = NULL;
    args.args[1] = NULL;
    args.args[2] = NULL;
    
    argp_parse (&argp, argc, argv, 0, 0, &args);

    blocksize = args.blocksize;

    verbose = args.verbose;    

    gettimeofday(&abs_start, NULL);

    print("=== %s ===\n", MESSAGE);

    print("\nPreparing...\n");

    prepare_threads(args, &n_threads);

    operation = prepare_fds(args.args[0], &args.args[1], args.args[2], &input_map, &input_filesize, &fds);

    // Initialize stream encoder:
    base64_stream_encode_init(&state, 0);

    print("\tInput file: %s\n\t\tFilesize: %ld bytes\n", args.args[0], input_filesize);

    print("\tOutput file: %s\n", args.args[1]);

    if(operation == COMPRESS)
    {
      print("\tDetected .mzML file, starting compression...\n");

      preprocess_mzml((char*)input_map, input_filesize, &divisions, &blocksize, &n_threads, &dp, &df, &binary_divisions, &xml_divisions);

      #if DEBUG == 1
        dprintf(fds[2], "=== Begin binary divisions ===\n");
        dump_divisions_to_file(binary_divisions, divisions, n_threads, fds[2]);
        dprintf(fds[2], "=== End binary divisions ===\n");
        dprintf(fds[2], "=== Begin XML divisions ===\n");
        dump_divisions_to_file(xml_divisions, divisions, n_threads, fds[2]);
        dprintf(fds[2], "=== End XML divisions ===\n");
      #endif

      write_header(fds[1], df->compression, "ZSTD", "ZSTD", blocksize, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

      compress_mzml((char*)input_map, blocksize, divisions, n_threads, &footer, dp, df, binary_divisions, xml_divisions, fds[1]);

    }

    else if (operation == DECOMPRESS)
    {
      print("\tDetected .msz file, reading header and footer...\n");

      block_len_queue_t* xml_blks;
      block_len_queue_t* binary_blks;
      data_positions_t* dp;
      footer_t* msz_footer;
      char* xml_compression_type;
      char* binary_compression_type;
      int binary_encoding;

      blocksize = get_header_blocksize(input_map);

      binary_encoding = get_header_binary_encoding(input_map);

      print("\tBinary_encoding: %d\n", binary_encoding);

      get_compression_scheme(input_map, &xml_compression_type, &binary_compression_type);

      parse_footer(&msz_footer, input_map, input_filesize, &xml_blks, &binary_blks, &dp);

      divisions = xml_blks->populated;
      binary_divisions = get_binary_divisions(dp, &blocksize, &divisions, &n_threads);
      xml_divisions = get_xml_divisions(dp, binary_divisions, divisions);
      
      #if DEBUG == 1
        dump_divisions_to_file(xml_divisions, divisions, n_threads, fds[2]);
      #endif

      print("\nDecompression and encoding...\n");

      decompress_parallel(input_map, binary_encoding, xml_blks, binary_blks, xml_divisions, msz_footer, divisions, n_threads, fds[1]);

    }

    dealloc_dp(dp);

    dealloc_df(df);

    free_ddp(xml_divisions, divisions);

    free_ddp(binary_divisions, divisions);
    
    remove_mapping(input_map, fds[0]);
    close(fds[0]);
    close(fds[1]);

    gettimeofday(&abs_stop, NULL);

    print("\n=== Operation finished in %1.4fs ===\n", (abs_stop.tv_sec-abs_start.tv_sec)+((abs_stop.tv_usec-abs_start.tv_usec)/1e+6));

    return 0;
}