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
#include <assert.h>

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
    { "lossy", 'l', "type", OPTION_ARG_OPTIONAL, "Enable lossy compression (cast, log). (disabled by default)"},
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

  if(!strcmp(prefix, "KB") || !strcmp(prefix, "kb"))
    res = num*1e+3;
  else if(!strcmp(prefix, "MB") || !strcmp(prefix, "mb"))
    res = num*1e+6;
  else if(!strcmp(prefix, "GB") || !strcmp(prefix, "gb"))
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
      if(arg == NULL)
        argp_error(state, "Invalid number of threads.");
      arguments->threads = atoi(arg);
      if(arguments->threads <= 0)
        argp_error(state, "Number of threads cannot be less than 1.");
      break;
    case 'l':
      if(arg == NULL)
        argp_error(state, "Invalid lossy compression type.");
      arguments->lossy = arg;
      break;  
    case 'b':
      if(arg == NULL)
        argp_error(state, "Invalid blocksize.");
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
//TODO
{      
  *xml_compression_type = "ZSTD";
  *binary_compression_type = "ZSTD";
  print("\tXML compression scheme: %.*s\n", 0, *xml_compression_type);
  print("\tBinary compression scheme: %.*s\n", 0, *binary_compression_type);
}

void
parse_footer(footer_t** footer, void* input_map, long input_filesize,
            block_len_queue_t**xml_block_lens,
            block_len_queue_t** mz_binary_block_lens,
            block_len_queue_t** inten_binary_block_lens,
            divisions_t** divisions,
            int* n_divisions)
{

      *footer = read_footer(input_map, input_filesize);

      print("\tXML position: %ld\n", (*footer)->xml_pos);
      print("\tm/z binary position: %ld\n", (*footer)->mz_binary_pos);
      print("\tint binary position: %ld\n", (*footer)->inten_binary_pos);
      print("\tXML blocks position: %ld\n", (*footer)->xml_blk_pos);
      print("\tm/z binary blocks position: %ld\n", (*footer)->mz_binary_blk_pos);
      print("\tinten binary blocks position: %ld\n", (*footer)->inten_binary_blk_pos);
      print("\tdivisions position: %ld\n", (*footer)->divisions_t_pos);
      print("\tEOF position: %ld\n", input_filesize);
      print("\tOriginal filesize: %ld\n", (*footer)->original_filesize);

      *xml_block_lens = read_block_len_queue(input_map, (*footer)->xml_blk_pos, (*footer)->mz_binary_blk_pos);
      *mz_binary_block_lens = read_block_len_queue(input_map, (*footer)->mz_binary_blk_pos, (*footer)->inten_binary_blk_pos);
      *inten_binary_block_lens = read_block_len_queue(input_map, (*footer)->inten_binary_blk_pos, (*footer)->divisions_t_pos);

      *n_divisions = (*footer)->n_divisions;

      *divisions = read_divisions(input_map, (*footer)->divisions_t_pos, *n_divisions);   
}

int 
main(int argc, char* argv[]) 
{
    struct arguments args;

    struct timeval abs_start, abs_stop;
    long blocksize = 0;
    struct base64_state state;

    data_positions_t **ddp, **mz_binary_divisions, **inten_binary_divisions, **xml_divisions; // TODO: remove

    divisions_t* divisions;
    data_format_t* df;

    int fds[3] = {-1, -1, -1};
    void* input_map = NULL;
    size_t input_filesize = 0;
    long n_threads = 0;
    int operation = -1;

    args.verbose = 0;
    // args.blocksize = 1e+10;
    args.blocksize = 1e+8;
    args.threads = 0;
    args.lossy = NULL;
    args.args[0] = NULL;
    args.args[1] = NULL;
    args.args[2] = NULL;
    args.args[3] = NULL;
    
    
    argp_parse (&argp, argc, argv, 0, 0, &args);

    if(args.lossy == NULL)
      args.lossy = "lossless";

    blocksize = args.blocksize;

    verbose = args.verbose;    

    gettimeofday(&abs_start, NULL);

    print("=== %s ===\n", MESSAGE);

    print("\nPreparing...\n");

    prepare_threads(args, &n_threads);

    // Open file descriptors and mmap.
    operation = prepare_fds(args.args[0], &args.args[1], args.args[2], &input_map, &input_filesize, &fds);

    // Initialize b64 encoder.
    base64_stream_encode_init(&state, 0);

    print("\tInput file: %s\n\t\tFilesize: %ld bytes\n", args.args[0], input_filesize);

    print("\tOutput file: %s\n", args.args[1]);

    if(operation == COMPRESS)
    {
      // Initialize footer to all 0's to not write garbage to file.
      footer_t* footer = calloc(1, sizeof(footer_t));   

      print("\tDetected .mzML file, starting compression...\n");

      // Scan mzML for position of all binary data. Divide the m/z, intensity, and XML data over threads.

      preprocess_mzml((char*)input_map,
                      input_filesize,
                      &blocksize,
                      n_threads,
                      &df,
                      &divisions);
      
      footer->original_filesize = input_filesize;
      footer->n_divisions = divisions->n_divisions; // Set number of divisions in footer.                

      // TODO: target format switch
      df->target_xml_format = _ZSTD_compression_;
      df->target_mz_format = _ZSTD_compression_;
      df->target_inten_format = _ZSTD_compression_;

      // Parse arguments for compression algorithms and set formats accordingly.
      
      // Store format integer in footer.
      footer->mz_fmt = get_algo_type(args.lossy);
      footer->inten_fmt = get_algo_type(args.lossy);
      
      // Set target compression functions.
      df->target_mz_fun= set_compress_algo(footer->mz_fmt);
      df->target_inten_fun = set_compress_algo(footer->inten_fmt);
      
      // Set decoding function based on source compression format.
      df->decode_source_compression_fun = set_decode_fun(df->source_compression, footer->mz_fmt);

      //Write df header to file.
      write_header(fds[1], df, blocksize, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

      //Start compress routine.
      compress_mzml((char*)input_map,
                    blocksize,
                    n_threads,
                    footer,
                    df,
                    divisions,
                    fds[1]);


      // Write divisions to file.
      footer->divisions_t_pos = get_offset(fds[1]);
      write_divisions(divisions, fds[1]);

      // Write footer to file.
      write_footer(footer, fds[1]);

      free(footer);
    }

    else if (operation == DECOMPRESS)
    {
      print("\tDetected .msz file, reading header and footer...\n");

      if(args.lossy != NULL)
        print("Lossy arg passed while decompressing, ignoring...\n");

      block_len_queue_t *xml_block_lens, *mz_binary_block_lens, *inten_binary_block_lens;
      footer_t* msz_footer;
      char* xml_compression_type;
      char* binary_compression_type;
      int n_divisions = 0;

      df = get_header_df(input_map);

      get_compression_scheme(input_map, &xml_compression_type, &binary_compression_type);

      parse_footer(&msz_footer, input_map, input_filesize,
                   &xml_block_lens, 
                   &mz_binary_block_lens,
                   &inten_binary_block_lens,
                   &divisions,
                   &n_divisions);

      if(n_divisions == 0)
        error("No divisions found in file, aborting...\n");

      print("\nDecompression and encoding...\n");

      df->encode_source_compression_fun = set_encode_fun(df->source_compression, msz_footer->mz_fmt);
      df->target_mz_fun = set_decompress_algo(msz_footer->mz_fmt);
      df->target_inten_fun = set_decompress_algo(msz_footer->inten_fmt);

      decompress_parallel(input_map,
                          xml_block_lens,
                          mz_binary_block_lens,
                          inten_binary_block_lens,
                          divisions,
                          df, msz_footer, n_threads, fds[1]);

    }
    print("\nCleaning up...\n");

    // free_ddp(xml_divisions, divisions);
    // free_ddp(mz_binary_divisions, divisions);
    // free_ddp(inten_binary_divisions, divisions);

    dealloc_df(df);

    remove_mapping(input_map, fds[0]);

    close(fds[0]);
    close(fds[1]);
    print("\tClosed file descriptors\n");

    gettimeofday(&abs_stop, NULL);

    print("\n=== Operation finished in %1.4fs ===\n", (abs_stop.tv_sec-abs_start.tv_sec)+((abs_stop.tv_usec-abs_start.tv_usec)/1e+6));

    return 0;
}