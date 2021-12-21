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

    argp_parse (&argp, argc, argv, 0, 0, &args);
    
    if(args.in_file == NULL || args.out_file == NULL)
        return -1;
    
    clock_t start, stop;

    start = clock();

    int input_fd, output_fd;
    
    input_fd = open(args.in_file, O_RDONLY);

    if(input_fd < 0)
    {
      fprintf(stderr, "Error in opening input file descriptor.");
      exit(1);
    }

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
    
    cpu_count = get_cpu_count();

    n_threads = cpu_count;

    int if_blksize = get_blksize(args.in_file);
    int of_blksize = get_blksize(args.out_file);

    struct base64_state state;

    // Initialize stream encoder:
    base64_stream_encode_init(&state, 0);

    printf("Input file: %s\tDevice blocksize: %d bytes\n", args.in_file, if_blksize);

    printf("Output file: %s\tDevice blocksize: %d bytes\n", args.out_file, of_blksize);

    data_format* df = pattern_detect((char*)input_map);

    if (df == NULL)
        return -1;
    // printf("%d %d %d %d\n", df->int_fmt, df->mz_fmt, df->compression, df->total_spec);


    data_positions* dp = find_binary((char*)input_map, df);
    if (dp == NULL)
        return -1;
    
    // get_encoded_lengths((char*) input_map, dp);
    
    
    stop = clock();

    // for (int i = 0; i < 10; i++) {
    //     printf("(%d, %d, %d)\n", dp->start_positions[i], dp->end_positions[i], dp->encoded_lengths[i]);
    // }

    printf("Detected %d spectra.\n", df->total_spec);

    printf("Preprocessing time: %1.4fs\n", (double)(stop-start) / CLOCKS_PER_SEC);  

    ZSTD_CCtx* zstd;
    
    zstd = alloc_cctx();

    start = clock();

    for (int i = 0; i < dp->total_spec; i++)
    {
      size_t decoded_binary_len = 0;
      size_t zstd_len = 0;
      double* bin = decode_binary((char*)input_map, dp->start_positions[i], dp->end_positions[i], df->compression, &decoded_binary_len);
      zstd_compress(zstd, bin, decoded_binary_len, &zstd_len, 1);
      // printf("spec no: %d\tdecoded_len: %ld\n", i, decoded_binary_len);
    }

    stop = clock();

    printf("Decoding and compression time: %1.4fs\n", (double)(stop-start) / CLOCKS_PER_SEC);

    write_header(output_fd);

    remove_mapping(input_map, input_fd);
    close(input_fd);
    close(output_fd);

    printf("=== Finish ===\n");

    return 0;
}