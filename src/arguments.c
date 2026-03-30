#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
   // Set target encoding and decompression functions.
   df->encode_source_compression_mz_fun = set_encode_fun(
       df->source_compression, msz_footer->mz_fmt, df->source_mz_fmt);
   df->encode_source_compression_inten_fun = set_encode_fun(
       df->source_compression, msz_footer->inten_fmt, df->source_inten_fmt);

   if (df->encode_source_compression_mz_fun == NULL ||
       df->encode_source_compression_inten_fun == NULL) {
      error("set_decompress_runtime_variables: Failed to set encode functions.\n");
      return 1;
   }

   // Set target decompression functions.
   df->target_mz_fun =
       set_decompress_algo(msz_footer->mz_fmt, df->source_mz_fmt);
   df->target_inten_fun =
       set_decompress_algo(msz_footer->inten_fmt, df->source_inten_fmt);

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