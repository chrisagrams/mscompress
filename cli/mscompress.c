#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <zstd.h>

#include "libbase64.h"
#include "yxml.h"
#include "mscompress.h"

static const char* program_name = NULL;

static void
print_usage(FILE* stream, int exit_code) {
  fprintf(stream, "Usage: %s [OPTION...] input_file [output_file]\n", program_name);
  fprintf(stream, "Compresses mass spec raw data with high efficiency.\n\n");
  fprintf(stream, "MSCompress version %s %s\n", VERSION, STATUS);
  fprintf(stream, "Supports msz versions %s-%s\n", MIN_SUPPORT, MAX_SUPPORT);
  fprintf(stream, "Options:\n");
  fprintf(stream, "  -v, --verbose                 Run in verbose mode.\n");
  fprintf(stream, "  -t, --threads num             Set amount of threads to use. (default: auto)\n");
  fprintf(stream, "  -z, --mz-lossy type           Enable mz lossy compression (cast, log, delta(16, 32), vbr). (disabled by default)\n");
  fprintf(stream, "  -i, --int-lossy type          Enable int lossy compression (cast, log, delta(16, 32), vbr). (disabled by default)\n");
  fprintf(stream, " --mz-scale-factor factor       Set mz scale factors for delta transform or threshold for vbr.\n");
  fprintf(stream, " --int-scale-factor factor      Set int scale factors for log transform or threshold for vbr\n");
  fprintf(stream, " --extract-indices [range]      Extract indices from mzML file (eg. [1-3,5-6]). (disabled by default)\n");
  fprintf(stream, " --extract-scans [range]        Extract scans from mzML file (eg. [1-3,5-6]). (disabled by default)\n");
  fprintf(stream, " --ms-level level               Extract specified ms level (1, 2, n). (disabled by default)\n");
  fprintf(stream, " --extract-only                 Only output extracted mzML, no compression (disabled by default)\n");
  fprintf(stream, " --target-xml-format type       Set target xml compression format (zstd, none). (default: zstd)\n");
  fprintf(stream, " --target-mz-format type        Set target mz compression format (zstd, none). (default: zstd)\n");
  fprintf(stream, " --target-inten-format type     Set target inten compression format (zstd, none). (default: zstd)\n");
  fprintf(stream, " --zstd-compression-level level Set zstd compression level (1-22). (default: 3)\n");
  fprintf(stream, "  -b, --blocksize size          Set maximum blocksize (xKB, xMB, xGB). (default: 100MB)\n");
  fprintf(stream, "  -c, --checksum                Enable checksum generation. (disabled by default)\n");
  fprintf(stream, "  -h, --help                    Show this help message.\n");
  fprintf(stream, "  -V, --version                 Show version information.\n\n");
  fprintf(stream, "Arguments:\n");
  fprintf(stream, "  input_file                    Input file path.\n");
  fprintf(stream, "  output_file                   Output file path. If not specified, the output file name is the input file name with extension .msz.\n\n");
  exit(exit_code);
}

static void validate_algo_name(const char* name) {
  if (strcmp(name, "cast")    != 0 &&
      strcmp(name, "cast16")  != 0 &&
      strcmp(name, "log")     != 0 &&
      strcmp(name, "delta16") != 0 &&
      strcmp(name, "delta24") != 0 &&
      strcmp(name, "delta32") != 0 &&
      strcmp(name, "vdelta16") != 0 &&
      strcmp(name, "vdelta24") != 0 &&
      strcmp(name, "vbr")     != 0 &&
      strcmp(name, "bitpack") != 0 )
  {
    fprintf(stderr, "Invalid lossy compression type: %s\n", name);
    print_usage(stderr, 1);
  }
}

static void 
parse_arguments(int argc, char* argv[], struct Arguments* arguments) {
  int i;
  arguments->verbose          = 0;
  arguments->threads          = 0;
  arguments->extract_only     = 0;
  arguments->mz_lossy         = NULL;
  arguments->int_lossy        = NULL;
  arguments->blocksize        = 1e+8;
  arguments->input_file       = NULL;
  arguments->output_file      = NULL;
  arguments->mz_scale_factor  = 1000; // initialize scale factor to default value
  arguments->int_scale_factor = 0;
  arguments->indices          = NULL;
  arguments->indices_length   = 0;
  arguments->scans            = NULL;
  arguments->scans_length     = 0;
  arguments->ms_level         = 0;

  arguments->target_xml_format   = _ZSTD_compression_; // default
  arguments->target_mz_format    = _ZSTD_compression_; // default
  arguments->target_inten_format = _ZSTD_compression_; // default

  arguments->zstd_compression_level = 3; // default

  program_name = argv[0];

  if(argc < 2) {
    print_usage(stderr, 1);
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
      arguments->verbose = 1;
    } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Invalid number of threads.");
        print_usage(stderr, 1);
      }
      arguments->threads = atoi(argv[++i]);
      if (arguments->threads <= 0) {
        fprintf(stderr, "%s\n", "Number of threads cannot be less than 1.");
        print_usage(stderr, 1);
      }
    } else if (strcmp(argv[i], "-z") == 0 || strcmp(argv[i], "--mz-lossy") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Invalid mz lossy compression type.");
        print_usage(stderr, 1);
      }
      arguments->mz_lossy = argv[++i];
      validate_algo_name(arguments->mz_lossy);
      if(strcmp(arguments->mz_lossy, "delta16") == 0)
        arguments->mz_scale_factor = 127.998046875;
      else if(strcmp(arguments->mz_lossy, "delta24") == 0)
        arguments->mz_scale_factor = 65536;
      else if(strcmp(arguments->mz_lossy, "delta32") == 0)
        arguments->mz_scale_factor = 262143.99993896484;
      else if(strcmp(arguments->mz_lossy, "vbr") == 0)
        arguments->mz_scale_factor = 0.1;
      else if(strcmp(arguments->mz_lossy, "bitpack") == 0)
        arguments->mz_scale_factor = 10000.0;
      else if(strcmp(arguments->mz_lossy, "cast16") == 0)
        arguments->mz_scale_factor = 11.801;
    } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--int-lossy") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Invalid int lossy compression type.");
        print_usage(stderr, 1);
      }
      arguments->int_lossy = argv[++i];
      validate_algo_name(arguments->int_lossy);
      if(strcmp(arguments->int_lossy, "log") == 0)
        arguments->int_scale_factor = 72.0;
      else if(strcmp(arguments->int_lossy, "vbr") == 0)
        arguments->int_scale_factor = 1.0;
    } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--blocksize") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Invalid blocksize.");
        print_usage(stderr, 1);
      }
      long blksize = parse_blocksize(argv[++i]);
      if (blksize == -1) {
        fprintf(stderr, "%s\n", "Unkown size suffix. (KB, MB, GB)");
        print_usage(stderr, 1);
      }
      arguments->blocksize = blksize;
    } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--checksum") == 0) {
      // enable checksum generation (not implemented)
    } else if (strcmp(argv[i], "--mz-scale-factor") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing scale factor for mz compression.");
        print_usage(stderr, 1);
      }
      
      const char* scale_factor_str = argv[++i];
      int j = 0;
      char scale_factor_buffer[20];

      // Parse the argument until the first non-digit character
      while (isdigit(scale_factor_str[j]) || scale_factor_str[j] == '.') {
        scale_factor_buffer[j] = scale_factor_str[j];
        j++;
      }
      scale_factor_buffer[j] = '\0'; // Null-terminate the parsed scale factor

      arguments->mz_scale_factor = atof(scale_factor_buffer);
    }
     else if (strcmp(argv[i], "--int-scale-factor") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing scale factor for inten compression.");
        print_usage(stderr, 1);
      }
      
      const char* scale_factor_str = argv[++i];
      int j = 0;
      char scale_factor_buffer[20];

      // Parse the argument until the first non-digit character
      while (isdigit(scale_factor_str[j]) || scale_factor_str[j] == '.') {
        scale_factor_buffer[j] = scale_factor_str[j];
        j++;
      }
      scale_factor_buffer[j] = '\0'; // Null-terminate the parsed scale factor

      arguments->int_scale_factor = atof(scale_factor_buffer);
    }
    else if (strcmp(argv[i], "--extract-indices") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing indices array for extraction.");
        print_usage(stderr, 1);
      }
      arguments->indices = string_to_array(argv[++i], &arguments->indices_length);
    } 
    else if (strcmp(argv[i], "--extract-scans") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing scan array for extraction.");
        print_usage(stderr, 1);
      }
      arguments->scans = string_to_array(argv[++i], &arguments->scans_length);
    }
    else if (strcmp(argv[i], "--ms-level") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing ms level for extraction.");
        print_usage(stderr, 1);
      }
      if(argv[++i] == 'n')
        arguments->ms_level = -1; //still valid, set to "n"
      else
      {
        arguments->ms_level = atoi(argv[i]);
        if(!(arguments->ms_level == 1 || arguments->ms_level == 2))
        {
          fprintf(stderr, "%s\n", "Invalid ms level for extraction.");
          print_usage(stderr, 1);
        }
      }
    }
    else if (strcmp(argv[i], "--extract-only") == 0) {
      arguments->extract_only = 1;
    }
    else if (strcmp(argv[i], "--target-xml-format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing target xml format.");
        print_usage(stderr, 1);
      }
      arguments->target_xml_format = get_compress_type(argv[++i]);
    } 
    else if (strcmp(argv[i], "--target-mz-format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing target mz format.");
        print_usage(stderr, 1);
      }
      arguments->target_mz_format = get_compress_type(argv[++i]);
    } 
    else if (strcmp(argv[i], "--target-inten-format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing target inten format.");
        print_usage(stderr, 1);
      }
      arguments->target_inten_format = get_compress_type(argv[++i]);
    }
    else if (strcmp(argv[i], "--zstd-compression-level") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s\n", "Missing compression level");
        print_usage(stderr, 1);
      }
      int num = 0;
      const char* str = argv[++i];
      while (*str >= '0' && *str <= '9') {
          num = num * 10 + (*str - '0');
          str++;
      }
      arguments->zstd_compression_level = num;
    }  
    else if (arguments->input_file == NULL) {
      arguments->input_file = argv[i];
    }
    else if (arguments->output_file == NULL) {
      arguments->output_file = argv[i];
    } 
    else {
      fprintf(stderr, "%s\n", "Too many arguments.");
      print_usage(stderr, 1);
    }
  }

  if (arguments->input_file == NULL) {
    fprintf(stderr, "%s\n", "Missing input file.");
    print_usage(stderr, 1);
  }

  if(arguments->mz_lossy == NULL)
      arguments->mz_lossy = "lossless";
  
  if(arguments->int_lossy == NULL)
      arguments->int_lossy = "lossless";
}

int 
main(int argc, char* argv[]) 
{
    struct Arguments arguments;

    double abs_start, abs_stop;
    struct base64_state state;

    divisions_t* divisions;
    data_format_t* df;

    void* input_map = NULL;
    size_t input_filesize = 0;
    int operation = -1;
    
    parse_arguments(argc, argv, &arguments);

    verbose = arguments.verbose;    

    abs_start = get_time();

    print("=== %s ===\n", MESSAGE);

    print("\nPreparing...\n");

    prepare_threads(&arguments); // Populate threads variable if not set.

    // Open file descriptors and mmap.
    operation = prepare_fds(arguments.input_file, &arguments.output_file, NULL, &input_map, &input_filesize, &fds);

    if(arguments.extract_only)
      operation = EXTRACT;

    // Initialize b64 encoder.
    base64_stream_encode_init(&state, 0);

    print("\tInput file: %s\n\t\tFilesize: %ld bytes\n", arguments.input_file, input_filesize);

    print("\tOutput file: %s\n", arguments.output_file);

    switch(operation)
    {
      case COMPRESS:
      {
        // Initialize footer to all 0's to not write garbage to file.
        footer_t* footer = calloc(1, sizeof(footer_t));   

        print("\tDetected .mzML file, starting compression...\n");

        // Scan mzML for position of all binary data. Divide the m/z, intensity, and XML data over threads.
        preprocess_mzml((char*)input_map,
                        input_filesize,
                        &(arguments.blocksize),
                        &arguments,
                        &df,
                        &divisions);
        
        footer->original_filesize = input_filesize;
        footer->n_divisions = divisions->n_divisions; // Set number of divisions in footer.                

        // Set target formats.
        df->target_xml_format   = arguments.target_xml_format;
        df->target_mz_format    = arguments.target_mz_format;
        df->target_inten_format = arguments.target_inten_format;

        // Set target compression functions.
        df->xml_compression_fun   = set_compress_fun(df->target_xml_format);
        df->mz_compression_fun    = set_compress_fun(df->target_mz_format);
        df->inten_compression_fun = set_compress_fun(df->target_inten_format);

        // Parse arguments for compression algorithms and set formats accordingly.
        
        // Store format integer in footer.
        footer->mz_fmt    = get_algo_type(arguments.mz_lossy);
        footer->inten_fmt = get_algo_type(arguments.int_lossy);
        
        // Set target compression functions.
        df->target_mz_fun    = set_compress_algo(footer->mz_fmt, df->source_mz_fmt);
        df->target_inten_fun = set_compress_algo(footer->inten_fmt, df->source_inten_fmt);
        
        // Set decoding function based on source compression format.
        df->decode_source_compression_mz_fun    = set_decode_fun(df->source_compression, footer->mz_fmt, df->source_mz_fmt);
        df->decode_source_compression_inten_fun = set_decode_fun(df->source_compression, footer->inten_fmt, df->source_inten_fmt);

        // Set ZSTD compression level.
        df->zstd_compression_level = arguments.zstd_compression_level; 

        // Set scale factor.
        df->mz_scale_factor = arguments.mz_scale_factor;
        df->int_scale_factor = arguments.int_scale_factor;

        //Write df header to file.
        write_header(fds[1], df, arguments.blocksize, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

        //Start compress routine.
        compress_mzml((char*)input_map,
                      arguments.blocksize,
                      arguments.threads,
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
        break;
      }
      case DECOMPRESS:
      {
        print("\tDetected .msz file, reading header and footer...\n");

        block_len_queue_t *xml_block_lens, *mz_binary_block_lens, *inten_binary_block_lens;
        footer_t* msz_footer;
        int n_divisions = 0;

        df = get_header_df(input_map);

        parse_footer(&msz_footer, input_map, input_filesize,
                    &xml_block_lens, 
                    &mz_binary_block_lens,
                    &inten_binary_block_lens,
                    &divisions,
                    &n_divisions);

        if(n_divisions == 0)
          error("No divisions found in file, aborting...\n");

        print("\nDecompression and encoding...\n");

        // Set target encoding and decompression functions.
        df->encode_source_compression_mz_fun    = set_encode_fun(df->source_compression, msz_footer->mz_fmt, df->source_mz_fmt);
        df->encode_source_compression_inten_fun = set_encode_fun(df->source_compression, msz_footer->inten_fmt, df->source_mz_fmt);

        df->target_mz_fun    = set_decompress_algo(msz_footer->mz_fmt, df->source_mz_fmt);
        df->target_inten_fun = set_decompress_algo(msz_footer->inten_fmt, df->source_inten_fmt);

        // Set target decompression functions.
        df->xml_decompression_fun   = set_decompress_fun(df->target_xml_format);
        df->mz_decompression_fun    = set_decompress_fun(df->target_mz_format);
        df->inten_decompression_fun = set_decompress_fun(df->target_inten_format);

        //Start decompress routine.
        decompress_parallel(input_map,
                            xml_block_lens,
                            mz_binary_block_lens,
                            inten_binary_block_lens,
                            divisions,
                            df, msz_footer, arguments.threads, fds[1]);

        break;
      };
      case EXTRACT:
      {
          print("\nExtracting ...\n");

          arguments.threads = -1, //force single threaded
          preprocess_mzml((char*)input_map,
                input_filesize,
                &(arguments.blocksize),
                &arguments,
                &df,
                &divisions);
          
          extract_mzml((char*)input_map, divisions, fds[1]);
      };
    }
    print("\nCleaning up...\n");

    // free_ddp(xml_divisions, divisions);
    // free_ddp(mz_binary_divisions, divisions);
    // free_ddp(inten_binary_divisions, divisions);

    dealloc_df(df);

    remove_mapping(input_map, fds[0]);

    close_file(fds[0]);
    close_file(fds[1]);
    print("\tClosed file descriptors\n");

    abs_stop = get_time();

    print("\n=== Operation finished in %1.4fs ===\n", abs_stop-abs_start);

    exit(0);
}