#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algos/algos.h"
#include "mscompress.h"

/**
* @brief Validates the provided lossy compression algorithm name.
* @param name The name of the lossy compression algorithm to validate.
* @return Returns 0 if the algorithm name is valid, 1 otherwise.
*/
static int validate_algo_name(const char* name) {
   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(name, algo_registry[i].name) == 0)
         return 0;
   }
   fprintf(stderr, "Invalid lossy compression type: %s\n", name);
   return 1;
}

/**
* @brief Initializes the `Arguments` struct with default values.
* @param args A pointer to the `Arguments` struct to initialize.
*/
void init_args(Arguments* args) {
   args->verbose = 0;
   args->threads = 0;
   args->extract_only = 0;
   args->describe_only = 0;
   args->mz_lossy = "lossless";   // default
   args->int_lossy = "lossless";  // default
   args->blocksize = 1e+8;
   args->input_file = NULL;
   args->output_file = NULL;
   args->mz_scale_factor = 1000;  // initialize scale factor to default value
   args->int_scale_factor = 0;
   args->indices = NULL;
   args->indices_length = 0;
   args->scans = NULL;
   args->scans_length = 0;
   args->ms_level = 0;

   args->target_xml_format = _ZSTD_compression_;    // default
   args->target_mz_format = _ZSTD_compression_;     // default
   args->target_inten_format = _ZSTD_compression_;  // default

   args->zstd_compression_level = 3;  // default

   args->shuffle = 1;  // default: on, opt out with --no-shuffle
   args->shuffle_explicit = 0;

   args->json_output = 0;

   // Batch mode defaults.
   args->batch = 0;
   args->recursive = 0;
   args->continue_on_error = 0;
   args->list_mode = 0;
   args->from_file = NULL;
   args->inputs = NULL;
   args->n_inputs = 0;
}

/**
* @brief Sets the number of threads to use.
* @param args A pointer to the `Arguments` struct.
* @param threads The number of threads to set.
* @return Returns 0 on success, 1 on error.
*/
int set_threads(Arguments* args, int threads) {
   if (threads < 0) {
      fprintf(stderr, "Invalid number of threads: %d\n", threads);
      return 1;  // Indicate error
   }
   if (threads == 0)  // default
      args->threads = 1;
   else
      args->threads = threads;
   return 0;  // Indicate success
}

/**
* @brief Prints a formatted table of available lossy transformation algorithms.
*/
static const char* target_str(int target) {
   if ((target & TARGET_MZ) && (target & TARGET_INT))
      return "mz/int";
   if (target & TARGET_MZ)
      return "mz";
   if (target & TARGET_INT)
      return "int";
   return "unknown";
}

static void scale_factor_str(char* buf, size_t buf_size, const algo_info_t* algo) {
   if ((algo->target & TARGET_MZ) && (algo->target & TARGET_INT)) {
      snprintf(buf, buf_size, "%g (mz), %g (int)",
               (double)algo->default_mz_scale, (double)algo->default_int_scale);
   } else if ((algo->target & TARGET_MZ) && algo->default_mz_scale != 0) {
      snprintf(buf, buf_size, "%g", (double)algo->default_mz_scale);
   } else if ((algo->target & TARGET_INT) && algo->default_int_scale != 0) {
      snprintf(buf, buf_size, "%g", (double)algo->default_int_scale);
   } else {
      snprintf(buf, buf_size, "N/A");
   }
}

void print_algorithms(void) {
   printf("Available lossy transformation algorithms:\n\n");
   printf("  %-12s %-10s %-45s %s\n", "Name", "Target", "Description", "Default Scale Factor");
   printf("  %-12s %-10s %-45s %s\n", "----", "------", "-----------", "--------------------");
   for (int i = 0; i < algo_registry_size; i++) {
      char scale_buf[64];
      scale_factor_str(scale_buf, sizeof(scale_buf), &algo_registry[i]);
      printf("  %-12s %-10s %-45s %s\n",
             algo_registry[i].name,
             target_str(algo_registry[i].target),
             algo_registry[i].description,
             scale_buf);
   }
   printf("\nUse -z/--mz-lossy or -i/--int-lossy to enable an algorithm.\n");
   printf("Use --mz-scale-factor or --int-scale-factor to override the default.\n");
}

/**
* @brief Prints available lossy transformation algorithms as a JSON array.
*/
void print_algorithms_json(void) {
   printf("[\n");
   for (int i = 0; i < algo_registry_size; i++) {
      const algo_info_t* a = &algo_registry[i];
      printf("  {\n");
      printf("    \"name\": \"%s\",\n", a->name);
      printf("    \"target\": \"%s\",\n", target_str(a->target));
      printf("    \"description\": \"%s\",\n", a->description);
      printf("    \"default_mz_scale\": %g,\n", (double)a->default_mz_scale);
      printf("    \"default_int_scale\": %g,\n", (double)a->default_int_scale);
      printf("    \"experimental\": %s\n", a->experimental ? "true" : "false");
      printf("  }%s\n", (i < algo_registry_size - 1) ? "," : "");
   }
   printf("]\n");
}

/**
* @brief Sets the lossy compression algorithm for mz data.
* @param args A pointer to the `Arguments` struct.
* @param mz_lossy The name of the lossy compression algorithm to set.
* @return Returns 0 on success, 1 on error.
*/
int set_mz_lossy(Arguments* args, const char* mz_lossy) {
   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(mz_lossy, algo_registry[i].name) == 0) {
         if (!(algo_registry[i].target & TARGET_MZ)) {
            fprintf(stderr, "Algorithm '%s' does not support mz target.\n",
                    mz_lossy);
            return 1;
         }
         args->mz_lossy = mz_lossy;
         if (algo_registry[i].default_mz_scale != 0)
            args->mz_scale_factor = algo_registry[i].default_mz_scale;
         return 0;
      }
   }
   fprintf(stderr, "Invalid mz lossy compression type: %s\n", mz_lossy);
   return 1;
}

/**
* @brief Sets the lossy compression algorithm for intensity data.
* @param args A pointer to the `Arguments` struct.
* @param int_lossy The name of the lossy compression algorithm to set.
* @return Returns 0 on success, 1 on error.
*/
int set_int_lossy(Arguments* args, const char* int_lossy) {
   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(int_lossy, algo_registry[i].name) == 0) {
         if (!(algo_registry[i].target & TARGET_INT)) {
            fprintf(stderr, "Algorithm '%s' does not support int target.\n",
                    int_lossy);
            return 1;
         }
         args->int_lossy = int_lossy;
         if (algo_registry[i].default_int_scale != 0)
            args->int_scale_factor = algo_registry[i].default_int_scale;
         return 0;
      }
   }
   fprintf(stderr, "Invalid int lossy compression type: %s\n", int_lossy);
   return 1;
}

/**
* @brief Parses a scale factor from a string.
* @param scale_factor_str The string containing the scale factor.
* @return The parsed scale factor as a double.
*/
double parse_scale_factor(const char* scale_factor_str) {
   int j = 0;
   char scale_factor_buffer[20];

   // Parse the argument until the first non-digit character
   while (isdigit(scale_factor_str[j]) || scale_factor_str[j] == '.') {
      scale_factor_buffer[j] = scale_factor_str[j];
      j++;
   }
   scale_factor_buffer[j] = '\0';  // Null-terminate the parsed scale factor

   return atof(scale_factor_buffer);
}

/**
* @brief Sets the mz scale factor.
* @param args A pointer to the `Arguments` struct.
* @param scale_factor_str The string containing the scale factor.
* @return Returns 0 on success, 1 on error.
*/
int set_mz_scale_factor(Arguments* args, const char* scale_factor_str) {
   if (scale_factor_str == NULL) {
      fprintf(stderr, "%s\n", "Missing scale factor for mz compression.");
      return 1;
   }

   args->mz_scale_factor = parse_scale_factor(scale_factor_str);
   return 0;
}

/**
* @brief Sets the intensity scale factor.
* @param args A pointer to the `Arguments` struct.
* @param scale_factor_str The string containing the scale factor.
* @return Returns 0 on success, 1 on error.
*/
int set_int_scale_factor(Arguments* args, const char* scale_factor_str) {
   if (scale_factor_str == NULL) {
      fprintf(stderr, "%s\n", "Missing scale factor for inten compression.");
      return 1;
   }

   args->int_scale_factor = parse_scale_factor(scale_factor_str);
   return 0;
}

/**
 * @brief Validates that scale factors are non-zero for algorithms that require them.
 * @param args A pointer to the `Arguments` struct to validate.
 * @return Returns 0 if valid, 1 if invalid (with error message via error callback).
 */
int validate_args(Arguments* args, char* err_buf, size_t err_buf_size) {
   if (args == NULL) {
      if (err_buf && err_buf_size > 0)
         snprintf(err_buf, err_buf_size, "validate_args: NULL arguments");
      return 1;
   }

   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(args->mz_lossy, algo_registry[i].name) == 0 &&
          (algo_registry[i].target & TARGET_MZ)) {
         if (algo_registry[i].default_mz_scale != 0 && args->mz_scale_factor == 0) {
            if (err_buf && err_buf_size > 0)
               snprintf(err_buf, err_buf_size,
                        "mz_scale_factor cannot be 0 for algorithm '%s' (default: %g)",
                        args->mz_lossy, (double)algo_registry[i].default_mz_scale);
            return 1;
         }
         break;
      }
   }

   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(args->int_lossy, algo_registry[i].name) == 0 &&
          (algo_registry[i].target & TARGET_INT)) {
         if (algo_registry[i].default_int_scale != 0 && args->int_scale_factor == 0) {
            if (err_buf && err_buf_size > 0)
               snprintf(err_buf, err_buf_size,
                        "int_scale_factor cannot be 0 for algorithm '%s' (default: %g)",
                        args->int_lossy, (double)algo_registry[i].default_int_scale);
            return 1;
         }
         break;
      }
   }

   return 0;
}

/**
 * @brief Sets the compression runtime variables for the given arguments and data format.
 * @param args A pointer to the `Arguments` struct.
 * @param df A pointer to the `data_format_t` struct.
 * @return Returns 0 on success, 1 on error.
 */
int set_compress_runtime_variables(Arguments* args, data_format_t* df) {
   if (args == NULL || df == NULL) {
      error("NULL passed to set_compress_runtime_variables\n");
      return 1;
   }
   int mz_fmt = get_algo_type(args->mz_lossy);
   int inten_fmt = get_algo_type(args->int_lossy);

   if (mz_fmt == -1) {
      error("set_compress_runtime_variables: Invalid mz lossy compression type: %s\n",
              args->mz_lossy);
      return 1;
   }
   if (inten_fmt == -1) {
      error("set_compress_runtime_variables: Invalid inten lossy compression type: %s\n",
              args->int_lossy);
      return 1;
   }

   // Set target compression functions.
   df->target_mz_fun = set_compress_algo(mz_fmt, df->source_mz_fmt);
   df->target_inten_fun = set_compress_algo(inten_fmt, df->source_inten_fmt);

   if (df->target_mz_fun == NULL || df->target_inten_fun == NULL) {
      error("set_compress_runtime_variables: Failed to set target compression functions.\n");
      return 1;
   }

   // Set decoding function based on source compression format.
   df->decode_source_compression_mz_fun =
       set_decode_fun(df->source_compression, mz_fmt, df->source_mz_fmt);
   df->decode_source_compression_inten_fun =
       set_decode_fun(df->source_compression, inten_fmt, df->source_inten_fmt);

   if (df->decode_source_compression_mz_fun == NULL ||
       df->decode_source_compression_inten_fun == NULL) {
      error("set_compress_runtime_variables: Failed to set decode functions.\n");
      return 1;
   }

   // Set target formats.
   df->target_xml_format = args->target_xml_format;
   df->target_mz_format = args->target_mz_format;
   df->target_inten_format = args->target_inten_format;

   // Set target compression functions.
   df->xml_compression_fun = set_compress_fun(df->target_xml_format);
   df->mz_compression_fun = set_compress_fun(df->target_mz_format);
   df->inten_compression_fun = set_compress_fun(df->target_inten_format);

   if (df->xml_compression_fun == NULL || df->mz_compression_fun == NULL ||
       df->inten_compression_fun == NULL) {
      error("set_compress_runtime_variables: Failed to set compression functions.\n");
      return 1;
   }

   // Set ZSTD compression level.
   df->zstd_compression_level = args->zstd_compression_level;

   // Set scale factor.
   df->mz_scale_factor = args->mz_scale_factor;
   df->int_scale_factor = args->int_scale_factor;

   /* Lossless path only: a lossy transform rewrites samples into an encoding
    * whose element width is not the source width (and for vbr/bitpack is not
    * fixed at all), so transposing on the source width would corrupt it. */
   df->mz_shuffle_elem = 0;
   df->inten_shuffle_elem = 0;

   if (args->shuffle) {
      if (mz_fmt == _lossless_)
         df->mz_shuffle_elem = fmt_elem_size(df->source_mz_fmt);
      if (inten_fmt == _lossless_)
         df->inten_shuffle_elem = fmt_elem_size(df->source_inten_fmt);

      /* Only for a caller who asked: on by default, so an unconditional warning
         would fire on every lossy run for something they never requested. */
      if (args->shuffle_explicit) {
         if (mz_fmt != _lossless_ || inten_fmt != _lossless_)
            warning(
                "--shuffle only applies to losslessly stored streams; skipped "
                "for the stream(s) using a lossy transform.\n");

         if ((mz_fmt == _lossless_ && df->mz_shuffle_elem == 0) ||
             (inten_fmt == _lossless_ && df->inten_shuffle_elem == 0))
            warning(
                "--shuffle skipped for a stream whose source data type has no "
                "fixed element width.\n");
      }
   }

   return 0;
}

/**
 * @brief Sets the decompression runtime variables for the given data format and footer. This function initializes the decompression functions based on the target formats specified in the footer.
 * @param df A pointer to the `data_format_t` struct to set the decompression variables for
 * @param msz_footer A pointer to the `footer_t` struct containing the target formats for the decompression functions
 * @return Returns 0 on success, 1 on error. If an error occurs, the function will print an error message to stderr.
 * @note This function modifies the `data_format_t` struct in place.
 */
int set_decompress_runtime_variables(data_format_t* df, footer_t* msz_footer) {
   /* Strip the shuffle marker before dispatching on the accession; older files
    * have the bit clear, so this is a no-op for them. */
   int mz_fmt = MSZ_ALGO(msz_footer->mz_fmt);
   int inten_fmt = MSZ_ALGO(msz_footer->inten_fmt);

   df->mz_shuffle_elem = MSZ_HAS_SHUFFLE(msz_footer->mz_fmt)
                             ? fmt_elem_size(df->source_mz_fmt)
                             : 0;
   df->inten_shuffle_elem = MSZ_HAS_SHUFFLE(msz_footer->inten_fmt)
                                ? fmt_elem_size(df->source_inten_fmt)
                                : 0;

   if ((MSZ_HAS_SHUFFLE(msz_footer->mz_fmt) && df->mz_shuffle_elem == 0) ||
       (MSZ_HAS_SHUFFLE(msz_footer->inten_fmt) &&
        df->inten_shuffle_elem == 0)) {
      error(
          "set_decompress_runtime_variables: file is byte-shuffled but the "
          "source data type has no fixed element width; cannot reverse it.\n");
      return 1;
   }

   // Set target encoding and decompression functions.
   df->encode_source_compression_mz_fun =
       set_encode_fun(df->source_compression, mz_fmt, df->source_mz_fmt);
   df->encode_source_compression_inten_fun =
       set_encode_fun(df->source_compression, inten_fmt, df->source_inten_fmt);

   if (df->encode_source_compression_mz_fun == NULL ||
       df->encode_source_compression_inten_fun == NULL) {
      error("set_decompress_runtime_variables: Failed to set encode functions.\n");
      return 1;
   }

   // Set target decompression functions.
   df->target_mz_fun = set_decompress_algo(mz_fmt, df->source_mz_fmt);
   df->target_inten_fun = set_decompress_algo(inten_fmt, df->source_inten_fmt);

   if (df->target_mz_fun == NULL || df->target_inten_fun == NULL) {
      error("set_decompress_runtime_variables: Failed to set target decompression functions.\n");
      return 1;
   }

   // Set target decompression functions.
   df->xml_decompression_fun = set_decompress_fun(df->target_xml_format);
   df->mz_decompression_fun = set_decompress_fun(df->target_mz_format);
   df->inten_decompression_fun = set_decompress_fun(df->target_inten_format);

   if (df->xml_decompression_fun == NULL || df->mz_decompression_fun == NULL ||
       df->inten_decompression_fun == NULL) {
      error("set_decompress_runtime_variables: Failed to set decompression functions.\n");
      return 1;
   }

   return 0;
}