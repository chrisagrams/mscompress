#include "mscompress.h"

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

#include "../vendor/zstd/lib/zstd.h"
#include "libbase64.h"
#include "yxml.h"

static const char* program_name = NULL;

static void print_usage(FILE* stream, int exit_code) {
   fprintf(stream, "Usage: %s [OPTION...] input_file [output_file]\n",
           program_name);
   fprintf(stream, "Compresses mass spec raw data with high efficiency.\n\n");
   fprintf(stream, "MSCompress version %s %s\n", VERSION, STATUS);
   fprintf(stream, "Supports msz versions %s-%s\n", MIN_SUPPORT, MAX_SUPPORT);
   fprintf(stream, "Options:\n");
   fprintf(stream, "  -v, --verbose                 Run in verbose mode.\n");
   fprintf(stream,
           "  -t, --threads num             Set amount of threads to use. "
           "(default: auto)\n");
   fprintf(stream,
           "  -z, --mz-lossy type           Enable mz lossy compression (cast, "
           "log, delta(16, 32), vbr). (disabled by default)\n");
   fprintf(
       stream,
       "  -i, --int-lossy type          Enable int lossy compression (cast, "
       "log, delta(16, 32), vbr). (disabled by default)\n");
   fprintf(stream,
           " --mz-scale-factor factor       Set mz scale factors for delta "
           "transform or threshold for vbr.\n");
   fprintf(stream,
           " --int-scale-factor factor      Set int scale factors for log "
           "transform or threshold for vbr\n");
   fprintf(stream,
           " --extract-indices [range]      Extract indices from mzML or msz "
           "file (eg. [1-3,5-6]). (disabled by default)\n");
   fprintf(
       stream,
       " --extract-scans [range]        Extract scans from mzML or msz file "
       "(eg. [1-3,5-6]). (disabled by default)\n");
   fprintf(stream,
           " --ms-level level               Extract specified ms level (1, 2, "
           "n) from mzML or msz file. (disabled by default)\n");
   fprintf(stream,
           " --extract                      Enables extraction mode for either "
           "mzML or msz files. (disabled by default)\n");
   fprintf(stream,
           " --target-xml-format type       Set target xml compression format "
           "(zstd, none). (default: zstd)\n");
   fprintf(stream,
           " --target-mz-format type        Set target mz compression format "
           "(zstd, none). (default: zstd)\n");
   fprintf(
       stream,
       " --target-inten-format type     Set target inten compression format "
       "(zstd, none). (default: zstd)\n");
   fprintf(stream,
           " --zstd-compression-level level Set zstd compression level (1-22). "
           "(default: 3)\n");
   fprintf(stream,
           "  -b, --blocksize size          Set maximum blocksize (xKB, xMB, "
           "xGB). (default: 100MB)\n");
   fprintf(stream,
           "  -c, --checksum                Enable checksum generation. "
           "(disabled by default)\n");
   fprintf(
       stream,
       "  -d, --describe                Print header/footer in CSV format\n");
   fprintf(stream, "  -h, --help                    Show this help message.\n");
   fprintf(stream,
           "  -V, --version                 Show version information.\n\n");
   fprintf(stream, "Arguments:\n");
   fprintf(stream, "  input_file                    Input file path.\n");
   fprintf(stream,
           "  output_file                   Output file path. If not "
           "specified, the "
           "output file name is the input file name with extension .msz.\n\n");
   exit(exit_code);
}

static int parse_arguments(int argc, char* argv[], Arguments* arguments) {
   int i;

   init_args(arguments);

   program_name = argv[0];

   if (argc < 2) {
      return 1;
   }

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
         arguments->verbose = 1;
      } else if (strcmp(argv[i], "-t") == 0 ||
                 strcmp(argv[i], "--threads") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Invalid number of threads.");
            return 1;
         }
         set_threads(arguments, atoi(argv[++i]));
      } else if (strcmp(argv[i], "-z") == 0 ||
                 strcmp(argv[i], "--mz-lossy") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Invalid mz lossy compression type.");
            return 1;
         }
         set_mz_lossy(arguments, argv[++i]);
      } else if (strcmp(argv[i], "-i") == 0 ||
                 strcmp(argv[i], "--int-lossy") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Invalid int lossy compression type.");
            return 1;
         }
         set_int_lossy(arguments, argv[++i]);
      } else if (strcmp(argv[i], "-b") == 0 ||
                 strcmp(argv[i], "--blocksize") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Invalid blocksize.");
            return 1;
         }
         long blksize = parse_blocksize(argv[++i]);
         if (blksize == -1) {
            fprintf(stderr, "%s\n", "Unkown size suffix. (KB, MB, GB)");
            print_usage(stderr, 1);
         }
         arguments->blocksize = blksize;
      } else if (strcmp(argv[i], "-c") == 0 ||
                 strcmp(argv[i], "--checksum") == 0) {
         // enable checksum generation (not implemented)
      } else if (strcmp(argv[i], "-d") == 0 ||
                 strcmp(argv[i], "--describe") == 0) {
         arguments->describe_only = 1;
      } else if (strcmp(argv[i], "--mz-scale-factor") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing scale factor for mz compression.");
            return 1;
         }
         if (set_mz_scale_factor(arguments, argv[++i]) != 0)
            return 1;
      } else if (strcmp(argv[i], "--int-scale-factor") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n",
                    "Missing scale factor for inten compression.");
            return 1;
         }
         if (set_int_scale_factor(arguments, argv[++i]) != 0)
            return 1;
      } else if (strcmp(argv[i], "--extract-indices") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing indices array for extraction.");
            return 1;
         }
         arguments->indices =
             string_to_array(argv[++i], &arguments->indices_length);
      } else if (strcmp(argv[i], "--extract-scans") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing scan array for extraction.");
            return 1;
         }
         arguments->scans =
             string_to_array(argv[++i], &arguments->scans_length);
      } else if (strcmp(argv[i], "--ms-level") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing ms level for extraction.");
            return 1;
         }
         if (argv[++i] == 'n')
            arguments->ms_level = -1;  // still valid, set to "n"
         else {
            arguments->ms_level = atoi(argv[i]);
            if (!(arguments->ms_level == 1 || arguments->ms_level == 2)) {
               fprintf(stderr, "%s\n", "Invalid ms level for extraction.");
               return 1;
            }
         }
      } else if (strcmp(argv[i], "--extract") == 0) {
         arguments->extract_only = 1;
      } else if (strcmp(argv[i], "--target-xml-format") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing target xml format.");
            return 1;
         }
         arguments->target_xml_format = get_compress_type(argv[++i]);
      } else if (strcmp(argv[i], "--target-mz-format") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing target mz format.");
            return 1;
         }
         arguments->target_mz_format = get_compress_type(argv[++i]);
      } else if (strcmp(argv[i], "--target-inten-format") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing target inten format.");
            return 1;
         }
         arguments->target_inten_format = get_compress_type(argv[++i]);
      } else if (strcmp(argv[i], "--zstd-compression-level") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing compression level");
            return 1;
         }
         int num = 0;
         const char* str = argv[++i];
         while (*str >= '0' && *str <= '9') {
            num = num * 10 + (*str - '0');
            str++;
         }
         arguments->zstd_compression_level = num;
      } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
         print_usage(stdout, 0);
      } else if (strcmp(argv[i], "-V") == 0 ||
                 strcmp(argv[i], "--version") == 0) {
         fprintf(stdout, "MSCompress version %s %s\n", VERSION, STATUS);
         fprintf(stdout, "Supports msz versions %s-%s\n", MIN_SUPPORT,
                 MAX_SUPPORT);
         exit(0);
      } else if (arguments->input_file == NULL) {
         arguments->input_file = argv[i];
      } else if (arguments->output_file == NULL) {
         arguments->output_file = argv[i];
      } else {
         fprintf(stderr, "%s\n", "Too many arguments.");
         return 1;
      }
   }

   if (arguments->input_file == NULL) {
      fprintf(stderr, "%s\n", "Missing input file.");
      return 1;
   }

   return 0;
}

int main(int argc, char* argv[]) {
   Arguments arguments;

   double abs_start, abs_stop;
   struct base64_state state;

   divisions_t* divisions;
   data_format_t* df;

   void* input_map = NULL;
   size_t input_filesize = 0;
   int operation = -1;
   int error_status = 0;  // If error occurred, indicate cleanup and non-zero
                          // exit code on exit.

   if (parse_arguments(argc, argv, &arguments))
      print_usage(stderr, 1);

   verbose = arguments.verbose;

   abs_start = get_time();

   print("=== %s ===\n", MESSAGE);

   print("\nPreparing...\n");

   prepare_threads(&arguments);  // Populate threads variable if not set.

   // Open file descriptors and mmap.
   if (arguments.describe_only) {
      fds[0] = open_input_file(arguments.input_file);
      input_map = get_mapping(fds[0]);
      input_filesize = get_filesize(arguments.input_file);
      if (input_filesize == 0) {
         warning("Error in opening input file. Is it a directory?\n");
         exit(1);
      }
   } else
      operation = prepare_fds(arguments.input_file, &arguments.output_file,
                              NULL, &input_map, &input_filesize, &fds);

   if (arguments.describe_only)
      operation = DESCRIBE;
   if (arguments.extract_only &&
       operation == DECOMPRESS)  // msz detected, extracting
      operation = EXTRACT_MSZ;
   else if (arguments.extract_only)  // mzML detected, extracting
      operation = EXTRACT;

   // Initialize b64 encoder.
   base64_stream_encode_init(&state, 0);

   print("\tInput file: %s\n\t\tFilesize: %ld bytes\n", arguments.input_file,
         input_filesize);

   print("\tOutput file: %s\n", arguments.output_file);

   switch (operation) {
      case COMPRESS: {
         print("\tDetected .mzML file, starting compression...\n");

         // Scan mzML for position of all binary data. Divide the m/z,
         // intensity, and XML data over threads.
         if (preprocess_mzml((char*)input_map, input_filesize,
                             &(arguments.blocksize), &arguments, &df,
                             &divisions)) {
            error_status = 1;
            break;
         }

         // Start compress routine.
         compress_mzml((char*)input_map, input_filesize, &arguments, df,
                       divisions, fds[1]);

         break;
      }
      case DECOMPRESS: {
         print("\nDecompression and encoding...\n");

         // Start decompress routine.
         decompress_msz(input_map, input_filesize, &arguments, fds[1]);

         break;
      };
      case EXTRACT: {
         print("\nExtracting ...\n");

         arguments.threads = -1,  // force single threaded
             preprocess_mzml((char*)input_map, input_filesize,
                             &(arguments.blocksize), &arguments, &df,
                             &divisions);

         extract_mzml((char*)input_map, divisions, fds[1]);
      };
      case EXTRACT_MSZ: {
         extract_msz((char*)input_map, input_filesize, arguments.indices,
                     arguments.indices_length, arguments.scans,
                     arguments.scans_length, arguments.ms_level, fds[1]);
      };
      case EXTERNAL: {
         preprocess_external((char*)input_map, input_filesize,
                             &(arguments.blocksize), &arguments, &df,
                             &divisions);
         compress_mzml((char*)input_map, input_filesize, &arguments, df,
                       divisions, fds[1]);
      }
      case DESCRIBE: {
         footer_t* footer = read_footer((char*)input_map, input_filesize);
         if (!footer)
            exit(1);
         print_footer_csv(footer);
      };
   }
   print("\nCleaning up...\n");

   // free_ddp(xml_divisions, divisions);
   // free_ddp(mz_binary_divisions, divisions);
   // free_ddp(inten_binary_divisions, divisions);

   // dealloc_df(df);

   remove_mapping(input_map, fds[0]);

   close_file(fds[0]);
   close_file(fds[1]);
   print("\tClosed file descriptors\n");

   abs_stop = get_time();

   print("\n=== Operation finished in %1.4fs ===\n", abs_stop - abs_start);

   if (error_status) {
      remove_file(arguments.output_file);
      exit(1);
   }

   exit(0);
}