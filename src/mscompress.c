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


int
determine_filetype(int fd)
{
  if(is_mzml(fd))
  {
    printf("\t.mzML file detected.\n");
    return COMPRESS;
  }
  else if(is_msz(fd))
  {
    printf("\t.msz file detected.\n");
    return DECOMPRESS;
  }  
  else
    fprintf(stderr, "Invalid input file.\n");
  return -1;

}

char*
change_extension(char* input, char* extension)
{
  char* x;
  char* r;

  r = (char*)malloc(sizeof(char)*strlen(input));

  strcpy(r, input);
  x = strrchr(r, '.');
  strcpy(x, extension);

  return r;
}

int
prepare_fds(char* input_path, char** output_path, char* debug_output, char** input_map, int* input_filesize, int* fds)
{
  int input_fd;
  int output_fd;
  int type;

  if(input_path)
    input_fd = open(input_path, O_RDONLY);
  else
  {
    fprintf(stderr, "No input file specified.");
    exit(1);
  }
  if(input_fd < 0)
  {
    fprintf(stderr, "Error in opening input file descriptor. (%s)\n", strerror(errno));
    exit(1);
  }

  if(debug_output)
  {
    fds[2] = open(debug_output, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);
    printf("\tDEBUG OUTPUT: %s\n", debug_output);
  }

  type = determine_filetype(input_fd);

  if(type != COMPRESS && type != DECOMPRESS)
    exit(1);

  fds[0] = input_fd;
  *input_map = get_mapping(input_fd);
  *input_filesize = get_filesize(input_path);

  if(*output_path)
  {
    output_fd = open(*output_path, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);
    if(output_fd < 0)
    {
      fprintf(stderr, "Error in opening output file descriptor. (%s)\n", strerror(errno));
      exit(1);
    }
    fds[1] = output_fd;
    return type;
  }
   

  if(type == COMPRESS)
    *output_path = change_extension(input_path, ".msz\0");
  else if(type == DECOMPRESS)
    *output_path = change_extension(input_path, ".mzML\0");

  output_fd = open(*output_path, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);
  if(output_fd < 0)
  {
    fprintf(stderr, "Error in opening output file descriptor. (%s)\n", strerror(errno));
    exit(1);
  }
  fds[1] = output_fd;

  return type;

}

void prepare_threads(struct arguments args, long* n_threads)
{
    long cpu_count;

    cpu_count = get_cpu_count();

    if(args.threads == 0)
      *n_threads = cpu_count;
    else
      *n_threads = args.threads;
    
    printf("\tUsing %d threads.\n", *n_threads);
}

int
preprocess_mzml(char* input_map, long input_filesize, int* divisions, long* blocksize, long n_threads, data_positions_t** dp, data_format_t** df, data_positions_t*** binary_divisions, data_positions_t*** xml_divisions)
{
  struct timeval start, stop;

  gettimeofday(&start, NULL);

  printf("\nPreprocessing...\n");

  *df = pattern_detect((char*)input_map);

  if (*df == NULL)
      return -1;

  *dp = find_binary((char*)input_map, *df);
  if (*dp == NULL)
      return -1;

  (*dp)->file_end = input_filesize;

  *binary_divisions = get_binary_divisions(*dp, blocksize, divisions, n_threads);

  *xml_divisions = get_xml_divisions(*dp, *binary_divisions, *divisions);

  gettimeofday(&stop, NULL);

  printf("Preprocessing time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));  

}

void 
compress_mzml(char* input_map,
              long blocksize,
              int divisions,
              int threads,
              footer_t* footer,
              data_positions_t* dp,
              data_format_t* df,
              data_positions_t** binary_divisions,
              data_positions_t** xml_divisions,
              int output_fd)
{

    block_len_queue_t* xml_block_lens;

    block_len_queue_t* binary_block_lens;

    struct timeval start, stop;

    gettimeofday(&start, NULL);

    printf("\nDecoding and compression...\n");

    footer->xml_pos = get_offset(output_fd);

    xml_block_lens = compress_parallel((char*)input_map, xml_divisions, NULL, blocksize, divisions, threads, output_fd);  /* Compress XML */

    footer->binary_pos = get_offset(output_fd);

    binary_block_lens = compress_parallel((char*)input_map, binary_divisions, df, blocksize, divisions, threads, output_fd); /* Compress binary */

    footer->xml_blk_pos = get_offset(output_fd);

    dump_block_len_queue(xml_block_lens, output_fd);

    footer->binary_blk_pos = get_offset(output_fd);

    dump_block_len_queue(binary_block_lens, output_fd);

    footer->dp_pos = get_offset(output_fd);

    dump_dp(dp, output_fd);

    footer->num_spectra = dp->total_spec;

    footer->file_end = dp->file_end;

    write_footer(*footer, output_fd);

    gettimeofday(&stop, NULL);

    printf("Decoding and compression time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));
}


void
get_compression_scheme(void* input_map, char** xml_compression_type, char** binary_compression_type)
{      
  *xml_compression_type = get_header_xml_compresssion_type(input_map);
  *binary_compression_type = get_header_binary_compression_type(input_map);

  printf("\tXML compression scheme: %.*s\n", XML_COMPRESSION_METHOD_SIZE, *xml_compression_type);
  printf("\tBinary compression scheme: %.*s\n", BINARY_COMPRESSION_METHOD_SIZE, *binary_compression_type);
}

void
parse_footer(footer_t** footer, void* input_map, long input_filesize,
            block_len_queue_t** xml_blks, block_len_queue_t** binary_blks, data_positions_t** dp)
{

      *footer = read_footer(input_map, input_filesize);

      printf("\tXML position: %ld\n", (*footer)->xml_pos);
      printf("\tBinary position: %ld\n", (*footer)->binary_pos);
      printf("\tXML blocks position: %ld\n", (*footer)->xml_blk_pos);
      printf("\tBinary blocks position: %ld\n", (*footer)->binary_blk_pos);
      printf("\tdp block position: %ld\n", (*footer)->dp_pos);
      printf("\tEOF position: %ld\n", input_filesize);
      printf("\tOriginal file end: %ld\n", (*footer)->file_end);

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
    args.blocksize = 1e+9;
    args.threads = 0;
    args.args[0] = NULL;
    args.args[1] = NULL;
    args.args[2] = NULL;
    
    argp_parse (&argp, argc, argv, 0, 0, &args);

    blocksize = args.blocksize;    

    gettimeofday(&abs_start, NULL);

    printf("=== %s ===\n", MESSAGE);

    printf("\nPreparing...\n");

    prepare_threads(args, &n_threads);

    operation = prepare_fds(args.args[0], &args.args[1], args.args[2], &input_map, &input_filesize, &fds);

    // Initialize stream encoder:
    base64_stream_encode_init(&state, 0);

    printf("\tInput file: %s\n\t\tFilesize: %ld bytes\n", args.args[0], input_filesize);

    printf("\tOutput file: %s\n", args.args[1]);

    if(operation == COMPRESS)
    {
      printf("\tDetected .mzML file, starting compression...\n");

      preprocess_mzml((char*)input_map, input_filesize, &divisions, &blocksize, n_threads, &dp, &df, &binary_divisions, &xml_divisions);

      #if DEBUG == 1
        dprintf(fds[2], "=== Begin binary divisions ===\n");
        dump_divisions_to_file(binary_divisions, divisions, n_threads, fds[2]);
        dprintf(fds[2], "=== End binary divisions ===\n");
        dprintf(fds[2], "=== Begin XML divisions ===\n");
        dump_divisions_to_file(xml_divisions, divisions, n_threads, fds[2]);
        dprintf(fds[2], "=== End XML divisions ===\n");
      #endif

      write_header(fds[1], "ZSTD", "ZSTD", blocksize, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

      compress_mzml((char*)input_map, blocksize, divisions, n_threads, &footer, dp, df, binary_divisions, xml_divisions, fds[1]);

    }

    else if (operation == DECOMPRESS)
    {
      printf("\tDetected .msz file, reading header and footer...\n");

      block_len_queue_t* xml_blks;
      block_len_queue_t* binary_blks;
      data_positions_t* dp;
      footer_t* msz_footer;
      char* xml_compression_type;
      char* binary_compression_type;

      blocksize = get_header_blocksize(input_map);

      get_compression_scheme(input_map, &xml_compression_type, &binary_compression_type);

      parse_footer(&msz_footer, input_map, input_filesize, &xml_blks, &binary_blks, &dp);

      divisions = xml_blks->populated;
      binary_divisions = get_binary_divisions(dp, &blocksize, &divisions, n_threads);
      xml_divisions = get_xml_divisions(dp, binary_divisions, divisions);
      dump_divisions_to_file(xml_divisions, divisions, n_threads, fds[2]); // debug


      // size_t len = 0;
      // block_len_t* xml_blk;
      // block_len_t* binary_blk;
      // off_t footer_xml_offset = msz_footer->xml_pos;
      // off_t footer_binary_offset = msz_footer->binary_pos;

      // char* output;

      // for(int i = 0; i < divisions; i++)
      // {
      //   xml_blk = pop_block_len(xml_blks);
      //   binary_blk = pop_block_len(binary_blks);

      //   output = (char*)decmp_routine(input_map, footer_xml_offset, footer_binary_offset, xml_divisions[i], xml_blk, binary_blk, &len);
        
      //   footer_xml_offset += xml_blk->compressed_size;
      //   footer_binary_offset += binary_blk->compressed_size;

      //   write_to_file(fds[1], output, len);
      // }

      printf("\nDecompression and encoding...\n");

      decompress_parallel(input_map, xml_blks, binary_blks, xml_divisions, msz_footer, divisions, n_threads, fds[1]);

    }

    dealloc_dp(dp);

    dealloc_df(df);

    free_ddp(xml_divisions, divisions);

    free_ddp(binary_divisions, divisions);
    
    remove_mapping(input_map, fds[0]);
    close(fds[0]);
    close(fds[1]);

    gettimeofday(&abs_stop, NULL);

    printf("\n=== Operation finished in %1.4fs ===\n", (abs_stop.tv_sec-abs_start.tv_sec)+((abs_stop.tv_usec-abs_start.tv_usec)/1e+6));

    return 0;
}