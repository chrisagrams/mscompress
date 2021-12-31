#include "mscompress.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <argp.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <zstd.h>
#include "vendor/base64/include/libbase64.h"


#include <stdbool.h>
#include "vendor/yxml/yxml.h"

const char *argp_program_version = VERSION;
const char *argp_program_bug_address = ADDRESS;
static char doc[] = "MSCompress is used to high efficiently compress mass spec raw data";
static struct argp_option options[] = { 
    { "mzml", 'i', "IN_FILE", 0, "mzml file path"},
    { "msz", 'o', "OUT_FILE", 0, "output msz file path"},
    { "version", 'v', 0, OPTION_ARG_OPTIONAL, "version"},

    { 0 } 
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;

  switch (key)
    {
    case 'v':
      arguments->version = 1;
      break;
    case 'i':
      arguments->in_file = arg;
      break;
    case 'o':
      arguments->out_file = arg;
      break;
    case ARGP_KEY_ARG:
      if (state->arg_num >= 0)
	{
	  argp_usage(state);
	}
      arguments->args[state->arg_num] = arg;
      break;
    case ARGP_KEY_END:
      if (state->arg_num < 0)
	{
	  argp_usage (state);
	}
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static char args_doc[] = "";

static struct argp argp = {options, parse_opt, args_doc, doc};

int 
main(int argc, char* argv[]) 
{
    struct arguments args;
    args.in_file = NULL;
    args.out_file = NULL;
    args.version = 0;

    long cpu_count, n_threads;

    size_t input_filesize;

    argp_parse (&argp, argc, argv, 0, 0, &args);
    
    if(args.in_file == NULL || args.out_file == NULL)
        return -1;
    
    clock_t start, stop, abs_start, abs_stop;

    start = abs_start = clock();

    int input_fd, output_fd;
    
    input_fd = open(args.in_file, O_RDONLY);
    
    if(input_fd < 0)
    {
      fprintf(stderr, "Error in opening input file descriptor.");
      exit(1);
    }

    input_filesize = get_filesize(args.in_file);


    output_fd = open(args.out_file, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    
    if(output_fd < 0)
    {
      fprintf(stderr, "Error in opening output file descriptor.");
      exit(1);
    }

    void* input_map = get_mapping(input_fd);

    if (input_map == NULL)
        return -1;

    printf("=== %s ===\n", MESSAGE);

    printf("\nPreparing...\n");
    
    cpu_count = get_cpu_count();

    n_threads = 8;

    int if_blksize = get_blksize(args.in_file);
    int of_blksize = get_blksize(args.out_file);

    int blocksize = 1e+7;

    struct base64_state state;

    // Initialize stream encoder:
    base64_stream_encode_init(&state, 0);

    printf("\tInput file: %s\n\t\tDevice blocksize: %d bytes\n\t\tFilesize: %ld bytes\n", args.in_file, if_blksize, input_filesize);

    printf("\tOutput file: %s\n\t\tDevice blocksize: %d bytes\n", args.out_file, of_blksize);

    printf("\nPreprocessing...\n");

    data_format* df = pattern_detect((char*)input_map);

    if (df == NULL)
        return -1;

    data_positions* dp = find_binary((char*)input_map, df);
    if (dp == NULL)
        return -1;

    dp->file_end = input_filesize;
      
    int divisions = 0;

    data_positions** binary_divisions;

    binary_divisions = get_binary_divisions(dp, &blocksize, &divisions, n_threads);
    
    stop = clock();

    printf("\tDetected %d spectra.\n", df->total_spec);

    printf("\tPreprocessing time: %1.4fs\n", (double)(stop-start) / CLOCKS_PER_SEC);  

    ZSTD_DCtx* dzstd;
    
    dzstd = alloc_dctx();

    write_header(output_fd, "ZSTD", "d8e89b7e0044e0164b1e853516b90a05");

    start = clock();

    printf("\nDecoding and compression...\n");

    cmp_blk_queue_t* compressed_xml = compress_xml(input_map, dp, blocksize, output_fd);

    compress_binary_parallel((char*)input_map, binary_divisions, df, blocksize, divisions, output_fd);

    // cmp_blk_queue_t* compressed_binary = compress_binary(input_map, dp, df, blocksize, output_fd);

    stop = clock();

    printf("\tDecoding and compression time: %1.4fs\n", (double)(stop-start) / CLOCKS_PER_SEC);

    remove_mapping(input_map, input_fd);
    close(input_fd);
    close(output_fd);

    abs_stop = clock();

    printf("\n=== Operation finished in %1.4fs ===\n", (double)(abs_stop-abs_start) / CLOCKS_PER_SEC);

    return 0;
}