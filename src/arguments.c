#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"

static int validate_algo_name(const char* name) {
   if (strcmp(name, "cast") != 0 && strcmp(name, "cast16") != 0 &&
       strcmp(name, "log") != 0 && strcmp(name, "delta16") != 0 &&
       strcmp(name, "delta24") != 0 && strcmp(name, "delta32") != 0 &&
       strcmp(name, "vdelta16") != 0 && strcmp(name, "vdelta24") != 0 &&
       strcmp(name, "vbr") != 0 && strcmp(name, "bitpack") != 0) {
      fprintf(stderr, "Invalid lossy compression type: %s\n", name);
      return 1;  // Indicate error
   }

   return 0;  // Indicate success
}

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

int set_mz_lossy(Arguments* args, const char* mz_lossy) {
   // Validate the algorithm name
   if (validate_algo_name(mz_lossy))
      return 1;

   // Assign the algorithm to arguments
   args->mz_lossy = mz_lossy;

   // Set the default scale factor based on the algorithm
   if (strcmp(mz_lossy, "delta16") == 0)
      args->mz_scale_factor = 127.998046875;
   else if (strcmp(mz_lossy, "delta24") == 0)
      args->mz_scale_factor = 65536;
   else if (strcmp(mz_lossy, "delta32") == 0)
      args->mz_scale_factor = 262143.99993896484;
   else if (strcmp(mz_lossy, "vbr") == 0)
      args->mz_scale_factor = 0.1;
   else if (strcmp(mz_lossy, "bitpack") == 0)
      args->mz_scale_factor = 10000.0;
   else if (strcmp(mz_lossy, "cast16") == 0)
      args->mz_scale_factor = 11.801;
   else {
      fprintf(stderr, "Invalid mz lossy compression type: %s\n", mz_lossy);
      return 1;  // Indicate error
   }
   return 0;  // Indicate success
}

int set_int_lossy(Arguments* args, const char* int_lossy) {
   // Validate the algorithm name
   if (validate_algo_name(int_lossy))
      return 1;

   // Assign the algorithm to arguments
   args->int_lossy = int_lossy;

   // Set the default scale factor based on the algorithm
   if (strcmp(args->int_lossy, "log") == 0)
      args->int_scale_factor = 72.0;
   else if (strcmp(args->int_lossy, "vbr") == 0)
      args->int_scale_factor = 1.0;
   else {
      fprintf(stderr, "Invalid int lossy compression type: %s\n", int_lossy);
      return 1;  // Indicate error
   }

   return 0;  // Indicate success
}

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

int set_mz_scale_factor(Arguments* args, const char* scale_factor_str) {
   if (scale_factor_str == NULL) {
      fprintf(stderr, "%s\n", "Missing scale factor for mz compression.");
      return 1;
   }

   args->mz_scale_factor = parse_scale_factor(scale_factor_str);
   return 0;
}

int set_int_scale_factor(Arguments* args, const char* scale_factor_str) {
   if (scale_factor_str == NULL) {
      fprintf(stderr, "%s\n", "Missing scale factor for inten compression.");
      return 1;
   }

   args->int_scale_factor = parse_scale_factor(scale_factor_str);
   return 0;
}

void set_compress_runtime_variables(Arguments* args, data_format_t* df) {
   if (args == NULL || df == NULL) {
      warning("NULL passed to set_compress_runtime_variables\n");
      return;
   }
   int mz_fmt = get_algo_type(args->mz_lossy);
   int inten_fmt = get_algo_type(args->int_lossy);

   // Set target compression functions.
   df->target_mz_fun = set_compress_algo(mz_fmt, df->source_mz_fmt);
   df->target_inten_fun = set_compress_algo(inten_fmt, df->source_inten_fmt);

   // Set decoding function based on source compression format.
   df->decode_source_compression_mz_fun =
       set_decode_fun(df->source_compression, mz_fmt, df->source_mz_fmt);
   df->decode_source_compression_inten_fun =
       set_decode_fun(df->source_compression, inten_fmt, df->source_inten_fmt);

   // Set target formats.
   df->target_xml_format = args->target_xml_format;
   df->target_mz_format = args->target_mz_format;
   df->target_inten_format = args->target_inten_format;

   // Set target compression functions.
   df->xml_compression_fun = set_compress_fun(df->target_xml_format);
   df->mz_compression_fun = set_compress_fun(df->target_mz_format);
   df->inten_compression_fun = set_compress_fun(df->target_inten_format);

   // Set ZSTD compression level.
   df->zstd_compression_level = args->zstd_compression_level;

   // Set scale factor.
   df->mz_scale_factor = args->mz_scale_factor;
   df->int_scale_factor = args->int_scale_factor;

   return;
}

void set_decompress_runtime_variables(data_format_t* df, footer_t* msz_footer) {
   // Set target encoding and decompression functions.
   df->encode_source_compression_mz_fun = set_encode_fun(
       df->source_compression, msz_footer->mz_fmt, df->source_mz_fmt);
   df->encode_source_compression_inten_fun = set_encode_fun(
       df->source_compression, msz_footer->inten_fmt, df->source_mz_fmt);

   df->target_mz_fun =
       set_decompress_algo(msz_footer->mz_fmt, df->source_mz_fmt);
   df->target_inten_fun =
       set_decompress_algo(msz_footer->inten_fmt, df->source_inten_fmt);

   // Set target decompression functions.
   df->xml_decompression_fun = set_decompress_fun(df->target_xml_format);
   df->mz_decompression_fun = set_decompress_fun(df->target_mz_format);
   df->inten_decompression_fun = set_decompress_fun(df->target_inten_format);

   return;
}