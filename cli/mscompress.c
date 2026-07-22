#include "mscompress.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
           "file (eg. 0-100 or [0-100]). (disabled by default)\n");
   fprintf(
       stream,
       " --extract-scans [range]        Extract scans from mzML or msz file "
       "(eg. 1-3,5-6 or [1-3,5-6]). (disabled by default)\n");
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
   fprintf(stream,
           "      --list-algorithms         List available lossy algorithms.\n");
   fprintf(stream,
           "      --json                    Output in JSON format (for "
           "--version, --describe, --list-algorithms).\n");
   fprintf(stream, "\nBatch mode (many mzML -> one .mszx archive):\n");
   fprintf(stream,
           "      --batch                   Compress all input mzML into one "
           ".mszx (inferred for dirs/globs/multiple inputs).\n");
   fprintf(stream,
           "  -r, --recursive               Recurse into subdirectories for "
           "directory inputs.\n");
   fprintf(stream,
           "      --from-file path          Read newline-separated input paths "
           "from a manifest ('-' = stdin).\n");
   fprintf(stream,
           "  -o, --output path             Output archive path (.mszx). "
           "Required output form for batch.\n");
   fprintf(stream,
           "      --continue-on-error       Skip a failed input instead of "
           "aborting the batch.\n");
   fprintf(stream,
           "      --list                    Print a .mszx table of contents "
           "and exit.\n");
   fprintf(stream, "\n");
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

static int append_input(Arguments* arguments, char* path) {
   char** tmp =
       realloc(arguments->inputs, (arguments->n_inputs + 1) * sizeof(char*));
   if (!tmp) {
      fprintf(stderr, "Out of memory collecting input paths.\n");
      return 1;
   }
   arguments->inputs = tmp;
   arguments->inputs[arguments->n_inputs++] = path;
   return 0;
}

static int parse_arguments(int argc, char* argv[], Arguments* arguments) {
   int i;

   init_args(arguments);

   program_name = argv[0];

   if (argc < 2) {
      return 1;
   }

   /* Pre-scan for --json so it takes effect before early-exit flags. */
   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--json") == 0) {
         arguments->json_output = 1;
         break;
      }
   }

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--json") == 0) {
         continue;  /* Already handled in pre-scan. */
      } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
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
             (uint32_t *)string_to_array(argv[++i], &arguments->scans_length);
      } else if (strcmp(argv[i], "--ms-level") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing ms level for extraction.");
            return 1;
         }
         if (strcmp(argv[++i], "n") == 0)
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
      } else if (strcmp(argv[i], "--list-algorithms") == 0) {
         if (arguments->json_output)
            print_algorithms_json();
         else
            print_algorithms();
         exit(0);
      } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
         print_usage(stdout, 0);
      } else if (strcmp(argv[i], "-V") == 0 ||
                 strcmp(argv[i], "--version") == 0) {
         if (arguments->json_output) {
            printf("{\n");
            printf("  \"version\": \"%s\",\n", VERSION);
            printf("  \"status\": \"%s\",\n", STATUS);
            printf("  \"min_support\": \"%s\",\n", MIN_SUPPORT);
            printf("  \"max_support\": \"%s\"\n", MAX_SUPPORT);
            printf("}\n");
         } else {
            fprintf(stdout, "MSCompress version %s %s\n", VERSION, STATUS);
            fprintf(stdout, "Supports msz versions %s-%s\n", MIN_SUPPORT,
                    MAX_SUPPORT);
         }
         exit(0);
      } else if (strcmp(argv[i], "--batch") == 0) {
         arguments->batch = 1;
      } else if (strcmp(argv[i], "-r") == 0 ||
                 strcmp(argv[i], "--recursive") == 0) {
         arguments->recursive = 1;
      } else if (strcmp(argv[i], "--flatten") == 0) {
         arguments->flatten = 1;
      } else if (strcmp(argv[i], "--preserve-tree") == 0) {
         arguments->flatten = 0;
      } else if (strcmp(argv[i], "--continue-on-error") == 0) {
         arguments->continue_on_error = 1;
      } else if (strcmp(argv[i], "--list") == 0) {
         arguments->list_mode = 1;
      } else if (strcmp(argv[i], "-o") == 0 ||
                 strcmp(argv[i], "--output") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing output path for -o/--output.");
            return 1;
         }
         arguments->output_file = argv[++i];
      } else if (strcmp(argv[i], "--from-file") == 0) {
         if (i + 1 >= argc) {
            fprintf(stderr, "%s\n", "Missing path for --from-file.");
            return 1;
         }
         arguments->from_file = argv[++i];
      } else {
         /* Positional. Collect every one into inputs[]; the first also seeds
          * input_file for the single-file code paths. Output resolution
          * (legacy 2nd positional vs -o vs batch default) happens in main(). */
         if (append_input(arguments, argv[i])) return 1;
         if (arguments->input_file == NULL) arguments->input_file = argv[i];
      }
   }

   if (arguments->input_file == NULL && arguments->from_file == NULL) {
      fprintf(stderr, "%s\n", "Missing input file.");
      return 1;
   }

   return 0;
}

/* Decide whether this invocation is a batch (folder/glob/list -> .mszx) request.
 *
 * Backward compatibility is the priority: the legacy two-positional form
 * `mscompress input output` must NOT be treated as batch. The key disambiguator
 * is that a batch needs 2+ *existing* input files, whereas in `input output`
 * the second positional is an output path that does not exist yet. */
static int is_batch_request(Arguments* a) {
   if (a->batch || a->from_file) return 1;

   /* -o something.mszx forces archive output (single- or multi-input). */
   if (a->output_file) {
      size_t ol = strlen(a->output_file);
      if (ol > 5 && strcmp(a->output_file + ol - 5, ".mszx") == 0) return 1;
   }

   /* Any directory or quoted-glob positional => batch. */
   for (size_t i = 0; i < a->n_inputs; ++i) {
      if (strpbrk(a->inputs[i], "*?[")) return 1;
      struct stat st;
      if (stat(a->inputs[i], &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR)
         return 1;
   }

   /* 2+ existing regular-file inputs (e.g. shell-expanded *.mzML) => batch. */
   if (a->n_inputs >= 2) {
      size_t existing = 0;
      for (size_t i = 0; i < a->n_inputs; ++i) {
         struct stat st;
         if (stat(a->inputs[i], &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG)
            existing++;
      }
      if (existing >= 2) return 1;
   }

   if (a->recursive) return 1;
   return 0;
}

int main(int argc, char* argv[]) {
   Arguments arguments;

   double abs_start, abs_stop;
   struct base64_state state;

   divisions_t* divisions;
   data_format_t* df;

   char* input_map = NULL;
   long input_filesize = 0;
   int local_fds[3] = {-1, -1, -1};
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

   // --list: print an .mszx table of contents (no decompression) and exit.
   if (arguments.list_mode) {
      if (!arguments.input_file) {
         fprintf(stderr, "--list requires a .mszx input file.\n");
         exit(1);
      }
      exit(list_mszx(arguments.input_file) == 0 ? 0 : 1);
   }

   // Batch mode: folder / glob / explicit list / --from-file -> one .mszx.
   int is_batch = is_batch_request(&arguments);

   // Legacy single-file forms: a second positional is the output path. Applies
   // to compress/decompress/.mszx-decompress alike (batch ignores this: all
   // positionals are inputs there).
   if (!is_batch) {
      if (arguments.output_file == NULL && arguments.n_inputs >= 2)
         arguments.output_file = arguments.inputs[1];
      if (arguments.n_inputs > 2) {
         fprintf(stderr, "%s\n", "Too many arguments.");
         exit(1);
      }
   }

   // Detect .mszx input — handled by a dedicated path that mmaps into the tar
   // and writes mzML + annotations into an output directory. Bypasses
   // prepare_fds entirely. (Batch requests are always compress-to-.mszx, never
   // .mszx decompression.)
   int is_mszx = 0;
   if (!is_batch && arguments.input_file) {
      size_t ilen = strlen(arguments.input_file);
      if (ilen > 5 &&
          strcmp(arguments.input_file + ilen - 5, ".mszx") == 0) {
         is_mszx = 1;
      }
   }

   // Open file descriptors and mmap.
   if (is_batch) {
      // All positionals (+ --from-file) are inputs; output comes from -o or a
      // default computed in compress_batch. Streams straight into the archive;
      // no prepare_fds / input mmap here.
      operation = COMPRESS_BATCH;
   } else if (is_mszx) {
      operation = DECOMPRESS_MSZX;
      // Default output directory is the input path with ".mszx" stripped.
      if (arguments.output_file == NULL) {
         size_t ilen = strlen(arguments.input_file);
         arguments.output_file = malloc(ilen - 5 + 1);
         if (!arguments.output_file) exit(1);
         memcpy(arguments.output_file, arguments.input_file, ilen - 5);
         arguments.output_file[ilen - 5] = '\0';
      }
   } else if (arguments.describe_only) {
      local_fds[0] = open_input_file(arguments.input_file);
      input_map = get_mapping(local_fds[0]);
      input_filesize = get_filesize(arguments.input_file);
      if (input_filesize == 0) {
         warning("Error in opening input file. Is it a directory?\n");
         exit(1);
      }
   } else {
      operation =
          prepare_fds(arguments.input_file, &arguments.output_file, NULL,
                      &input_map, &input_filesize, local_fds);

      // If error occurred during prepare_fds, exit.
      if (operation < 0) {
         exit(1);
      }
   }

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
         if (preprocess_mzml(input_map, input_filesize,
                             &(arguments.blocksize), &arguments, &df,
                             &divisions)) {
            error_status = 1;
            break;
         }

         // Start compress routine.
         if (compress_mzml(input_map, input_filesize, &arguments, df,
                           divisions, local_fds[1])) {
            error_status = 1;
            break;
         }

         break;
      }
      case DECOMPRESS: {
         print("\nDecompression and encoding...\n");

         // Start decompress routine.
         if (decompress_msz(input_map, input_filesize, &arguments, local_fds[1])) {
            error_status = 1;
            break;
         }

         break;
      };
      case EXTRACT: {
         print("\nExtracting ...\n");

         arguments.threads = -1,  // force single threaded
             preprocess_mzml(input_map, input_filesize,
                             &(arguments.blocksize), &arguments, &df,
                             &divisions);

         extract_mzml(input_map, divisions, local_fds[1]);
         break;
      };
      case EXTRACT_MSZ: {
         extract_msz(input_map, input_filesize, arguments.indices,
                     arguments.indices_length, arguments.scans,
                     arguments.scans_length, arguments.ms_level,
                     local_fds[1]);
         break;
      };
      case EXTERNAL: {
         if (preprocess_external(input_map, input_filesize,
                                 &(arguments.blocksize), &arguments, &df,
                                 &divisions)) {
            error_status = 1;
            break;
         }

         if (compress_mzml(input_map, input_filesize, &arguments, df,
                           divisions, local_fds[1])) {
            error_status = 1;
            break;
         }
         break;
      }
      case DESCRIBE: {
         footer_t* footer = read_footer(input_map, input_filesize);
         if (!footer)
            exit(1);
         if (arguments.json_output)
            print_footer_json(footer);
         else
            print_footer_csv(footer);
         break;
      };
      case DECOMPRESS_MSZX: {
         print("\tDetected .mszx archive, extracting to %s\n",
               arguments.output_file);
         if (decompress_mszx(arguments.input_file, arguments.output_file,
                             &arguments)) {
            error_status = 1;
         }
         break;
      };
      case COMPRESS_BATCH: {
         // Batch compress: folder/glob/list -> one .mszx (Option A streaming).
         // compress_batch owns its output file lifecycle (incl. cleanup).
         if (compress_batch(&arguments)) {
            error_status = 1;
         }
         break;
      };
   }
   print("\nCleaning up...\n");

   // free_ddp(xml_divisions, divisions);
   // free_ddp(mz_binary_divisions, divisions);
   // free_ddp(inten_binary_divisions, divisions);

   // dealloc_df(df);

   remove_mapping(input_map, input_filesize);

   close_file(local_fds[0]);
   close_file(local_fds[1]);
   print("\tClosed file descriptors\n");

   abs_stop = get_time();

   print("\n=== Operation finished in %1.4fs ===\n", abs_stop - abs_start);

   if (error_status) {
      // compress_batch already cleans up its own (possibly defaulted) output.
      if (operation != COMPRESS_BATCH)
         remove_file(arguments.output_file);
      exit(1);
   }

   exit(0);
}