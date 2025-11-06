#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/zstd/lib/zstd.h"
#include "mscompress.h"

/*
    @section Decoding functions
*/

void algo_decode_lossless(void* args)
/**
 * @brief Lossless decoding function.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args->src == NULL)
      error("algo_decode_lossless: src is NULL");
#endif

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, a_args->dest,
                   a_args->dest_len, a_args->tmp);

   /* Lossless, don't touch anything */

   return;
}

void algo_decode_cast32_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   float* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_cast32_64d: Unknown data format");
#endif
   len = decoded_len / sizeof(double);
   res = malloc(
       (len + 1) *
       sizeof(float));  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_cast32: malloc failed");
#endif

   double* f = (double*)(decoded);
   for (int i = 1; i < len + 1; i++) {
      res[i] = (float)f[i - 1];
   }

   // Store length of array in first 4 bytes
   res[0] = (float)len;

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = (len + 1) * sizeof(float);

   return;
}

void algo_decode_cast16_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_cast16_32f: Unknown data format");
#endif
   len = decoded_len / sizeof(float);

   res = calloc(1, (len * sizeof(uint16_t)) +
                       sizeof(uint16_t));  // Allocate space for result and
                                           // leave room for header

   uint16_t* tmp = res + 1;  // Skip header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_cast16: malloc failed");
#endif

   float* f = (float*)(decoded);

   uint64_t uint_tmp;
   for (int i = 0; i < len; i++) {
      uint_tmp = (uint64_t)(f[i] * a_args->scale_factor);
      if (uint_tmp > UINT16_MAX)
         tmp[i] = UINT16_MAX;
      else
         tmp[i] = uint_tmp;
   }

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = (len * sizeof(uint16_t)) + sizeof(uint16_t);

   return;
}

void algo_decode_cast16_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_cast32_64d: Unknown data format");
#endif
   len = decoded_len / sizeof(double);

   res = calloc(1, (len * sizeof(uint16_t)) +
                       sizeof(uint16_t));  // Allocate space for result and
                                           // leave room for header

   uint16_t* tmp = res + 1;  // Skip header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_cast16: malloc failed");
#endif

   double* f = (double*)(decoded);

   uint64_t uint_tmp;
   for (int i = 0; i < len; i++) {
      uint_tmp = (uint64_t)(f[i] * a_args->scale_factor);
      if (uint_tmp > UINT16_MAX)
         tmp[i] = UINT16_MAX;
      else
         tmp[i] = uint_tmp;
   }

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = (len * sizeof(uint16_t)) + sizeof(uint16_t);

   return;
}

void algo_decode_log_2_transform_32f(void* args)
/**
 * @brief Log2 transform decoding function.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_log_2_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len = (len + 1) * sizeof(uint16_t);

   // Perform log2 transform
   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_log_2_transform: malloc failed");
#endif
   double ltran;

   float* f = (float*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   for (int i = 0; i < len; i++) {
      ltran = log2(f[i] + 1);  // Add 1 to avoid log2(0) = -inf
      tmp[i] = floor(ltran * a_args->scale_factor);
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_log_2_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_log_2_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   // Perform log2 transform
   res = malloc(
       (len + 1) *
       sizeof(
           uint16_t));  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_log_2_transform: malloc failed");
#endif

   double ltran;

   double* f = (double*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   for (int i = 0; i < len; i++) {
      ltran = log2(f[i] + 1);  // Add 1 to avoid log2(0) = -inf
      tmp[i] = floor(ltran * a_args->scale_factor);
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = (len + 1) * sizeof(uint16_t);

   return;
}

void algo_decode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   float* f = (float*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Perform delta transform
   float diff;
   uint16_t uint_diff;
   for (int i = 1; i < len; i++) {
      diff = f[i] - f[i - 1];
      uint_diff =
          (uint16_t)floor(diff * a_args->scale_factor);  // scale by 2^16 / 10
      tmp[i - 1] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = malloc(res_len);  // Allocate space for result and leave room for
                           // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   double* f = (double*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(double));

   tmp += 4;  // Move pointer to next value

   // Perform delta transform
   float diff;
   uint16_t uint_diff;
   for (int i = 1; i < len; i++) {
      diff = f[i] - f[i - 1];
      if (diff * a_args->scale_factor > UINT16_MAX) {
         // print("algo_decode_delta16_transform_64d: CLIPPING. diff: %0.4f,
         // scale_factor*diff: %0.4f\n", diff, diff*a_args->scale_factor);
         uint_diff = UINT16_MAX;
      } else
         uint_diff = (uint16_t)floor(
             diff * a_args->scale_factor);  // scale by 2^16 / 10
      tmp[i - 1] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len =
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(res_len, 1);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   float* f = (float*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(float));

   tmp += 2;  // Move pointer to next value

   uint8_t* dest = (uint8_t*)tmp;

   // Perform delta transform
   float diff;
   uint32_t uint_diff;

   int index = 0;  // index within dest

   for (int i = 1; i < len; i++) {
      diff = f[i] - f[i - 1];
      if (floor(diff * a_args->scale_factor) > 16777215)  // UINT24 max
         uint_diff = 16777215;
      else
         uint_diff = (uint32_t)floor(diff * a_args->scale_factor);
      dest[index * 3] = (uint_diff >> 16) & 0xFF;
      dest[index * 3 + 1] = (uint_diff >> 8) & 0xFF;
      dest[index * 3 + 2] = (uint_diff) & 0xFF;
      index++;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = calloc(res_len, 1);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   double* f = (double*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(double));

   tmp += 4;  // Move pointer to next value

   uint8_t* dest = (uint8_t*)tmp;

   // Perform delta transform
   float diff;
   uint32_t uint_diff;

   int index = 0;  // index within dest

   for (int i = 1; i < len; i++) {
      diff = f[i] - f[i - 1];
      if (floor(diff * a_args->scale_factor) > 16777215)  // UINT24 max
         uint_diff = 16777215;
      else
         uint_diff = (uint32_t)floor(diff * a_args->scale_factor);
      dest[index * 3] = (uint_diff >> 16) & 0xFF;
      dest[index * 3 + 1] = (uint_diff >> 8) & 0xFF;
      dest[index * 3 + 2] = (uint_diff) & 0xFF;
      index++;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint32_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   float* f = (float*)(decoded);
   uint32_t* tmp =
       (uint32_t*)((uint8_t*)res + 2);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(float));

   // Perform delta transform
   for (int i = 1; i < len; i++) {
      float diff = f[i] - f[i - 1];
      // uint16_t uint_diff = (diff > 0) ? (uint16_t)floor(diff) : 0; // clamp
      // to 0 if diff is negative
      uint32_t uint_diff =
          (uint32_t)floor(diff * a_args->scale_factor);  // scale by 2^16 / 10
      tmp[i] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint32_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   double* f = (double*)(decoded);
   uint32_t* tmp =
       (uint32_t*)((uint8_t*)res + 2);  // Ignore header in first 4 bytes

   // Store first value with full 32-bit precision
   //  *(float*)&res[0] = f[0];
   memcpy(tmp, f, sizeof(double));

   tmp = (uint32_t*)((uint8_t*)tmp + sizeof(double));

   // Perform delta transform
   for (int i = 1; i < len; i++) {
      float diff = f[i] - f[i - 1];
      // uint16_t uint_diff = (diff > 0) ? (uint16_t)floor(diff) : 0; // clamp
      // to 0 if diff is negative
      uint32_t uint_diff =
          (uint32_t)floor(diff * a_args->scale_factor);  // scale by 2^16 / 10
      tmp[i - 1] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_vdelta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_vdelta16_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   float* f = (float*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   float* diff_arr = (float*)malloc(len * sizeof(float));
   diff_arr[0] = f[0];

   double diff_max = 0;

   for (int i = 1; i < len; i++) {
      diff_arr[i] = f[i] - f[i - 1];
      if (diff_arr[i] > diff_max)
         diff_max = diff_arr[i];
   }

   float scale_factor = UINT16_MAX / (float)diff_max;

   // Store first value with 32-bit precision

   float starting = (float)f[0];
   memcpy(tmp, &starting, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Store scale_factor in next 4 bytes
   memcpy(tmp, &scale_factor, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Perform delta transform

   uint16_t uint_diff;
   for (int i = 1; i < len; i++) {
      uint_diff = (uint16_t)floor(diff_arr[i] * scale_factor);
      tmp[i - 1] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_vdelta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   double* f = (double*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   double* diff_arr = (double*)malloc(len * sizeof(double));
   diff_arr[0] = f[0];

   double diff_max = 0;

   for (int i = 1; i < len; i++) {
      diff_arr[i] = f[i] - f[i - 1];
      if (diff_arr[i] > diff_max)
         diff_max = diff_arr[i];
   }

   float scale_factor = UINT16_MAX / (float)diff_max;

   // Store first value with 32-bit precision

   float starting = (float)f[0];
   memcpy(tmp, &starting, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Store scale_factor in next 4 bytes
   memcpy(tmp, &scale_factor, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Perform delta transform

   uint16_t uint_diff;
   for (int i = 1; i < len; i++) {
      uint_diff = (uint16_t)floor(diff_arr[i] * scale_factor);
      tmp[i - 1] = uint_diff;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_vdelta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_vdelta24_transform_32f: Unknown data format");
#endif

   len = decoded_len / sizeof(float);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_vdelta24_transform_32f: malloc failed");
#endif

   float* f = (float*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   double* diff_arr = (double*)malloc(len * sizeof(double));
   diff_arr[0] = f[0];

   double diff_max = 0;

   for (int i = 1; i < len; i++) {
      diff_arr[i] = f[i] - f[i - 1];
      if (diff_arr[i] > diff_max)
         diff_max = diff_arr[i];
   }

   float scale_factor = 16777215 / (float)diff_max;  // UINT24_MAX

   // Store first value with 32-bit precision

   float starting = (float)f[0];
   memcpy(tmp, &starting, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Store scale_factor in next 4 bytes
   memcpy(tmp, &scale_factor, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Perform delta transform

   uint8_t* dest = (uint8_t*)tmp;

   int index = 0;  // index within dest

   uint32_t uint_diff;
   for (int i = 1; i < len; i++) {
      if (floor(diff_arr[i] * scale_factor) > 16777215)  // UINT24 max
         uint_diff = 16777215;
      else
         uint_diff = (uint32_t)floor(diff_arr[i] * scale_factor);
      dest[index * 3] = (uint_diff >> 16) & 0xFF;
      dest[index * 3 + 1] = (uint_diff >> 8) & 0xFF;
      dest[index * 3 + 2] = (uint_diff) & 0xFF;
      index++;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_vdelta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_delta_transform_64d: Unknown data format");
#endif

   len = decoded_len / sizeof(double);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_delta_transform: malloc failed");
#endif

   double* f = (double*)(decoded);
   uint16_t* tmp = (uint16_t*)(res + 1);  // Ignore header in first 4 bytes

   double* diff_arr = (double*)malloc(len * sizeof(double));
   diff_arr[0] = f[0];

   double diff_max = 0;

   for (int i = 1; i < len; i++) {
      diff_arr[i] = f[i] - f[i - 1];
      if (diff_arr[i] > diff_max)
         diff_max = diff_arr[i];
   }

   float scale_factor = 16777215 / (float)diff_max;  // UINT24_MAX

   // Store first value with 32-bit precision

   float starting = (float)f[0];
   memcpy(tmp, &starting, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Store scale_factor in next 4 bytes
   memcpy(tmp, &scale_factor, sizeof(float));

   tmp += 2;  // Move pointer to next value

   // Perform delta transform

   uint8_t* dest = (uint8_t*)tmp;

   int index = 0;  // index within dest

   uint32_t uint_diff;
   for (int i = 1; i < len; i++) {
      if (floor(diff_arr[i] * scale_factor) > 16777215)  // UINT24 max
         uint_diff = 16777215;
      else
         uint_diff = (uint32_t)floor(diff_arr[i] * scale_factor);
      dest[index * 3] = (uint_diff >> 16) & 0xFF;
      dest[index * 3 + 1] = (uint_diff >> 8) & 0xFF;
      dest[index * 3 + 2] = (uint_diff) & 0xFF;
      index++;
   }

   // Free decoded buffer
   free(decoded);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = res_len;

   return;
}

void algo_decode_vbr_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_vbr_32f: Unknown data format");
#endif

   if (decoded_len + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) >
       UINT32_MAX)
      error("algo_decode_vbr_32f: decoded_len > UINT32_MAX");

   len = (uint32_t)decoded_len;

   float* f = (float*)(decoded);

   float threshold = a_args->scale_factor;

   float base_peak_intensity = 0;
   // Get base peak intensity (max)
   for (int i = 0; i < len / sizeof(float); i++) {
      if (f[i] > base_peak_intensity)
         base_peak_intensity = f[i];
   }

   int num_bits = ceil(
       log2((base_peak_intensity / threshold) +
            1));  // number of bits required to represent base peak intensity

   if (num_bits == 1)
      num_bits = 2;  // 1 bit is not enough

   uint32_t res_len = (int)ceil(len / 4 * num_bits / 8) + sizeof(uint32_t) +
                      sizeof(float) + sizeof(uint32_t) + 1;

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_vbr_64d: malloc failed");
#endif

   unsigned char* tmp_res = res + sizeof(uint32_t) + sizeof(float) +
                            sizeof(uint32_t);  // Ignore header

   uint32_t bytes_used = 0;
   int bit_index = 0;
   int result_index = 0;
   unsigned char tmp_buff[8];
   int tmp_index = 0;

   for (int i = 0; i < len; i++) {
      tmp_buff[tmp_index] = decoded[i];
      tmp_index++;

      if (tmp_index == sizeof(float)) {
         float float32;
         memcpy(&float32, tmp_buff, sizeof(float));
         uint32_t float_int =
             (uint32_t)(float32 / base_peak_intensity * (exp2(num_bits) - 1));

         for (int j = 0; j < num_bits; j++) {
            int bit = (float_int >> j) & 1;
            tmp_res[bit_index >> 3] |= (bit << (bit_index & 7));
            bit_index++;
         }

         result_index++;
         tmp_index = 0;
      }
   }

   if (tmp_index == sizeof(float)) {
      float float32;
      memcpy(&float32, tmp_buff, sizeof(float));
      uint32_t float_int =
          (uint32_t)(float32 / base_peak_intensity * (exp2(num_bits) - 1));

      for (int j = 0; j < num_bits; j++) {
         int bit = (float_int >> j) & 1;
         tmp_res[bit_index >> 3] |= (bit << (bit_index & 7));
         bit_index++;
      }

      result_index++;
      tmp_index = 0;
   }

   bytes_used = (bit_index + 7) >> 3;

   int padding = (bytes_used * 8) - bit_index;
   for (int i = bit_index; i < bit_index + padding; i++) {
      tmp_res[i >> 3] &= ~(1 << (i & 7));
   }

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint32_t));
   // Store base peak intensity in next 8 bytes
   memcpy(res + sizeof(uint32_t), &base_peak_intensity, sizeof(float));
   // Store number of bytes in next 4 bytes
   memcpy(res + sizeof(uint32_t) + sizeof(float), &bytes_used,
          sizeof(uint32_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len =
       sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t) + bytes_used;

   return;
}

void algo_decode_vbr_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_vbr_64d: Unknown data format");
#endif

   if (decoded_len + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) >
       UINT32_MAX)
      error("algo_decode_vbr_64d: decoded_len > UINT32_MAX");

   len = (uint32_t)decoded_len;

   double* f = (double*)(decoded);

   double threshold = (double)a_args->scale_factor;

   double base_peak_intensity = 0;
   // Get base peak intensity (max)
   for (int i = 0; i < len / sizeof(double); i++) {
      if (f[i] > base_peak_intensity)
         base_peak_intensity = f[i];
   }

   int num_bits = ceil(
       log2((base_peak_intensity / threshold) +
            1));  // number of bits required to represent base peak intensity

   if (num_bits == 1)
      num_bits = 2;  // 1 bit is not enough

   uint32_t res_len = (int)ceil(len / 4 * num_bits / 8) + sizeof(uint32_t) +
                      sizeof(double) + sizeof(uint32_t);

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_vbr_64d: malloc failed");
#endif

   unsigned char* tmp_res = res + sizeof(uint32_t) + sizeof(double) +
                            sizeof(uint32_t);  // Ignore header

   uint32_t bytes_used = 0;
   int bit_index = 0;
   int result_index = 0;
   unsigned char tmp_buff[8];
   int tmp_index = 0;

   for (int i = 0; i < len; i++) {
      tmp_buff[tmp_index] = decoded[i];
      tmp_index++;

      if (tmp_index == sizeof(double)) {
         double float64;
         memcpy(&float64, tmp_buff, sizeof(double));
         uint64_t float_int =
             (uint64_t)(float64 / base_peak_intensity * (exp2(num_bits) - 1));

         for (int j = 0; j < num_bits; j++) {
            int bit = (float_int >> j) & 1;
            tmp_res[bit_index / 8] |= (bit << (bit_index % 8));
            bit_index++;
         }

         result_index++;
         tmp_index = 0;
      }
   }

   if (tmp_index == sizeof(double)) {
      double float64;
      memcpy(&float64, tmp_buff, sizeof(double));
      uint64_t float_int =
          (uint64_t)(float64 / base_peak_intensity * (exp2(num_bits) - 1));

      for (int j = 0; j < num_bits; j++) {
         int bit = (float_int >> j) & 1;
         tmp_res[bit_index / 8] |= (bit << (bit_index % 8));
         bit_index++;
      }

      result_index++;
      tmp_index = 0;
   }

   bytes_used = (bit_index + 7) / 8;

   int padding = (bytes_used * 8) - bit_index;
   for (int i = bit_index; i < bit_index + padding; i++) {
      tmp_res[i / 8] &= ~(1 << (i % 8));
   }

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint32_t));
   // Store base peak intensity in next 8 bytes
   memcpy(res + sizeof(uint32_t), &base_peak_intensity, sizeof(double));
   // Store number of bytes in next 4 bytes
   memcpy(res + sizeof(uint32_t) + sizeof(double), &bytes_used,
          sizeof(uint32_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len =
       sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + bytes_used;

   return;
}

void algo_decode_bitpack_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_decode_bitpack_32f: Unknown data format");
#endif

   size_t header_size = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   if (decoded_len + header_size > UINT32_MAX)
      error("algo_decode_bitpack_32f: decoded_len > UINT32_MAX");

   len = (uint32_t)(decoded_len / sizeof(float));

   float* f = (float*)(decoded);

   uint8_t num_bits = 27;  // TODO: add as argument

   uint32_t expected_bytes =
       (uint32_t)ceil(len * ((float)num_bits / 8));  // Bytes expected to use

   uint32_t res_len = expected_bytes + header_size;

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_vbr_64d: malloc failed");
#endif

   unsigned char* tmp_res = res + header_size;  // Ignore header

   int bit_index = 0;
   float scaled = 0;

   for (int i = 0; i < len; i++) {
      scaled = f[i] / a_args->scale_factor;

      if (scaled > 1.0)
         scaled = 1.0;  // clipping
      else if (scaled <= 0)
         scaled =
             a_args->scale_factor /
             (exp2(num_bits) - 1);  // if <= 0, set to smallest possible value

      uint64_t float_int = (uint64_t)(scaled * (exp2(num_bits) - 1));

      for (int j = 0; j < num_bits; j++) {
         int bit = (float_int >> j) & 1;
         tmp_res[bit_index >> 3] |= (bit << (bit_index % 8));
         bit_index++;
      }
   }

   // Pad the last byte with 0's
   uint32_t bytes_used = (bit_index + 7) / 8;

   int padding = (bytes_used * 8) - bit_index;
   for (int i = bit_index; i < bit_index + padding; i++) {
      tmp_res[i / 8] &= ~(1 << (i % 8));
   }

   // Store header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint32_t));
   // Store num bits in next byte
   memcpy(res + sizeof(uint32_t), &num_bits, sizeof(uint8_t));
   // Store number of bytes in next 4 bytes
   memcpy(res + sizeof(uint32_t) + sizeof(uint8_t), &bytes_used,
          sizeof(uint32_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = header_size + bytes_used;

   return;
}

void algo_decode_bitpack_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_decode_vbr_64d: Unknown data format");
#endif

   size_t header_size = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   if (decoded_len + header_size > UINT32_MAX)
      error("algo_decode_vbr_64d: decoded_len > UINT32_MAX");

   len = (uint32_t)(decoded_len / sizeof(double));

   double* f = (double*)(decoded);

   uint8_t num_bits = 27;  // TODO: add as argument

   uint32_t expected_bytes =
       (uint32_t)ceil(len * ((double)num_bits / 8));  // Bytes expected to use

   uint32_t res_len = expected_bytes + header_size;

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_decode_vbr_64d: malloc failed");
#endif

   unsigned char* tmp_res = res + header_size;  // Ignore header

   int bit_index = 0;
   double scaled = 0;

   for (int i = 0; i < len; i++) {
      scaled = f[i] / a_args->scale_factor;

      if (scaled > 1.0)
         scaled = 1.0;  // clipping
      else if (scaled <= 0)
         scaled =
             a_args->scale_factor /
             (exp2(num_bits) - 1);  // if <= 0, set to smallest possible value

      uint64_t float_int = (uint64_t)(scaled * (exp2(num_bits) - 1));

      for (int j = 0; j < num_bits; j++) {
         int bit = (float_int >> j) & 1;
         tmp_res[bit_index >> 3] |= (bit << (bit_index % 8));
         bit_index++;
      }
   }

   // Pad the last byte with 0's
   uint32_t bytes_used = (bit_index + 7) / 8;

   int padding = (bytes_used * 8) - bit_index;
   for (int i = bit_index; i < bit_index + padding; i++) {
      tmp_res[i / 8] &= ~(1 << (i % 8));
   }

   // Store header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint32_t));
   // Store num bits in next byte
   memcpy(res + sizeof(uint32_t), &num_bits, sizeof(uint8_t));
   // Store number of bytes in next 4 bytes
   memcpy(res + sizeof(uint32_t) + sizeof(uint8_t), &bytes_used,
          sizeof(uint32_t));

   // Return result
   *a_args->dest = res;
   *a_args->dest_len = header_size + bytes_used;

   return;
}

/*
    @section Encoding functions
*/

void algo_encode_lossless(void* args)
/**
 * @brief Lossless encoding function.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_lossless: args is NULL");
#endif

   /* Lossless, don't touch anything */

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, a_args->src, a_args->src_len, a_args->dest,
                   a_args->dest_len);

   return;
}

void algo_encode_cast32_64d(void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_cast32: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   float* arr = (float*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_cast32: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_cast32_64d: Unknown data format");
#endif

   // Allocate buffer
   void* res = malloc(sizeof(double) * len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_cast32: malloc failed");
#endif

   double* res_arr = (double*)res;

   // Cast all
   for (size_t i = 1; i < len + 1; i++) res_arr[i - 1] = (double)arr[i];

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len * sizeof(double), a_args->dest,
                   a_args->dest_len);

   // Move src pointer
   *a_args->src += (len + 1) * sizeof(float);

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_cast16_32f(void* args)
/**
 * @brief Casts 16-bit float array to 32-bit float array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_cast16_32f: args is NULL");
#endif

   // Cast 16-bit to 64-bit

   // Get source array
   uint16_t* arr = (uint16_t*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_cast16_32f: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_cast16_32f: Unknown data format");
#endif

   // Allocate buffer
   void* res = malloc(sizeof(float) * len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_cast16_32f: malloc failed");
#endif

   uint16_t* tmp = arr + 1;  // Ignore header

   float* res_arr = (float*)res;

   int i;

   for (i = 0; i < len; i++)
      res_arr[i] = (float)(tmp[i] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len * sizeof(float), a_args->dest,
                   a_args->dest_len);

   // Move src pointer
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t);

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_cast16_64d(void* args)
/**
 * @brief Casts 16-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_cast32: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   uint16_t* arr = (uint16_t*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_cast32: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_cast32_64d: Unknown data format");
#endif

   // Allocate buffer
   void* res = malloc(sizeof(double) * len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_cast32: malloc failed");
#endif

   uint16_t* tmp = arr + 1;  // Ignore header

   double* res_arr = (double*)res;

   int i;

   for (i = 0; i < len; i++)
      res_arr[i] = (double)(tmp[i] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len * sizeof(double), a_args->dest,
                   a_args->dest_len);

   // Move src pointer
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t);

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_log_2_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_log_2_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_log_2_transform: len is <= 0");
#endif

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_log_2_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_log_2_transform: malloc failed");
#endif

   // Perform log2 transform
   for (size_t i = 0; i < len; i++)
      res[i] = (float)exp2((double)arr[i] / a_args->scale_factor) - 1;

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += ((len + 1) * sizeof(uint16_t));

   return;
}

void algo_encode_log_2_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_log_2_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_log_2_transform: len is <= 0");
#endif

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_log_2_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);
   if (res == NULL)
      error("algo_encode_log_2_transform: malloc failed");
   // Perform log2 transform
   for (size_t i = 0; i < len; i++)
      res[i] = (double)exp2((double)arr[i] / a_args->scale_factor) - 1;

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += ((len + 1) * sizeof(uint16_t));

   return;
}

void algo_encode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint16_t* arr =
       (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((float)arr[i - 1] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(float);

   return;
}

void algo_encode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(double));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((double)arr[i - 1] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(double);

   return;
}

void algo_encode_vdelta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_vdelta16_transform_32f: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_vdelta16_transform_32f: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(float) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((float)arr[i - 1] / scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(float) +
                   sizeof(float);

   return;
}

void algo_encode_vdelta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(float) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((double)arr[i - 1] / scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(float) +
                   sizeof(float);

   return;
}

void algo_encode_vdelta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_vdelta24_transform_32f: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_vdelta24_transform_32f: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                             sizeof(float) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_vdelta24_transform_32f: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_vdelta24_transform_32f: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;

   int index = 0;  // index within arr

   uint32_t value;
   float diff;

   for (size_t i = 1; i < len; i++) {
      value = (arr[index * 3] << 16) | (arr[index * 3 + 1] << 8) |
              (arr[index * 3 + 2]);
      diff = (float)value / scale_factor;
      res[i] = res[i - 1] + diff;
      index++;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                   sizeof(float) + sizeof(float);

   return;
}

void algo_encode_vdelta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                             sizeof(float) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;

   int index = 0;  // index within arr

   uint32_t value;
   float diff;

   for (size_t i = 1; i < len; i++) {
      value = (arr[index * 3] << 16) | (arr[index * 3 + 1] << 8) |
              (arr[index * 3 + 2]);
      diff = (float)value / scale_factor;
      res[i] = res[i - 1] + diff;
      index++;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                   sizeof(float) + sizeof(float);

   return;
}

void algo_encode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint8_t* arr =
       (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;

   int index = 0;  // index within arr

   uint32_t value;
   float diff;

   for (size_t i = 1; i < len; i++) {
      value = (arr[index * 3] << 16) | (arr[index * 3 + 1] << 8) |
              (arr[index * 3 + 2]);
      diff = (float)value / a_args->scale_factor;
      res[i] = res[i - 1] + diff;
      index++;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src +=
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(float);

   return;
}

void algo_encode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint8_t* arr =
       (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(double));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;

   int index = 0;  // index within arr

   uint32_t value;
   float diff;

   for (size_t i = 1; i < len; i++) {
      value = (arr[index * 3] << 16) | (arr[index * 3 + 1] << 8) |
              (arr[index * 3 + 2]);
      diff = (float)value / a_args->scale_factor;
      res[i] = res[i - 1] + diff;
      index++;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src +=
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(double);

   return;
}

void algo_encode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint32_t* arr =
       (uint32_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((float)arr[i - 1] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(float);

   return;
}

void algo_encode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_delta_transform: args is NULL");
#endif

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_delta_transform: len is <= 0");
#endif

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint32_t* arr = (uint32_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(double));

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_delta_transform: Unknown data format");
#endif

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_delta_transform: malloc failed");
#endif

   // Perform delta transform
   res[0] = start;
   for (size_t i = 1; i < len; i++)
      res[i] = res[i - 1] + ((double)arr[i - 1] / a_args->scale_factor);

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest,
                   a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(double);

   return;
}

void algo_encode_vbr_32f(void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_vbr_32f: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t);

   // Get array length
   uint32_t len = *(uint32_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_vbr_32f: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_vbr_32f: Unknown data format");
#endif

   // Allocate buffer
   void* res = calloc(1, len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_vbr_64d: malloc failed");
#endif

   float* res_arr = (float*)res;

   float base_peak_intensity =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint32_t));

   uint32_t num_bytes = *(uint32_t*)((uint8_t*)(*a_args->src) +
                                     sizeof(uint32_t) + sizeof(float));

   double threshold = (double)a_args->scale_factor;

   int num_bits = ceil(log2((base_peak_intensity / threshold) + 1));

   if (num_bits == 1)
      num_bits = 2;  // 1 bit is not enough

   int b = 0;

   int result_index = 0;

   uint64_t tmp_int = 0;

   int tmp_int_bit_index = 0;

   int res_len = (int)ceil(num_bytes * 8);  // in bits

   for (int i = 0; i < res_len; i++) {
      int value = (tmp_arr[b >> 3] & (1 << (b & 7))) != 0;
      if (tmp_int_bit_index == num_bits && result_index * 4 < len) {
         res_arr[result_index] =
             (float)(tmp_int * base_peak_intensity) / (exp2(num_bits) - 1);
         result_index++;
         tmp_int_bit_index = 0;
      }
      if (value)
         tmp_int |= 1 << tmp_int_bit_index;
      else
         tmp_int &= ~(1 << tmp_int_bit_index);
      tmp_int_bit_index++;
      b++;
   }
   if (tmp_int_bit_index == num_bits && result_index * 4 < len) {
      res_arr[result_index] =
          (float)(tmp_int * base_peak_intensity) / (exp2(num_bits) - 1);
      result_index++;
      tmp_int_bit_index = 0;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len, a_args->dest, a_args->dest_len);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_vbr_64d(void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_vbr_64d: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t);

   // Get array length
   uint32_t len = *(uint32_t*)(*a_args->src);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_vbr_64d: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_vbr_64d: Unknown data format");
#endif

   // Allocate buffer
   void* res = calloc(1, len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_vbr_64d: malloc failed");
#endif

   double* res_arr = (double*)res;

   double base_peak_intensity =
       *(double*)((uint8_t*)(*a_args->src) + sizeof(uint32_t));

   uint32_t num_bytes = *(uint32_t*)((uint8_t*)(*a_args->src) +
                                     sizeof(uint32_t) + sizeof(double));

   double threshold = (double)a_args->scale_factor;

   int num_bits = ceil(log2((base_peak_intensity / threshold) + 1));

   if (num_bits == 1)
      num_bits = 2;  // 1 bit is not enough

   int b = 0;

   int result_index = 0;

   uint64_t tmp_int = 0;

   int tmp_int_bit_index = 0;

   int res_len = (int)ceil(num_bytes * 8);  // in bits

   for (int i = 0; i < res_len; i++) {
      int value = (tmp_arr[b / 8] & (1 << (b % 8))) != 0;
      if (tmp_int_bit_index == num_bits && result_index * 8 < len) {
         res_arr[result_index] =
             (double)(tmp_int * base_peak_intensity) / (exp2(num_bits) - 1);
         result_index++;
         tmp_int_bit_index = 0;
      }
      if (value)
         tmp_int |= 1 << tmp_int_bit_index;
      else
         tmp_int &= ~(1 << tmp_int_bit_index);
      tmp_int_bit_index++;
      b++;
   }
   if (tmp_int_bit_index == num_bits && result_index * 8 < len) {
      res_arr[result_index] =
          (double)(tmp_int * base_peak_intensity) / (exp2(num_bits) - 1);
      result_index++;
      tmp_int_bit_index = 0;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len, a_args->dest, a_args->dest_len);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_bitpack_32f(void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_bitpack_32f: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   // Get array length (in bytes)
   uint32_t len = *(uint32_t*)(*a_args->src) * sizeof(float);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_bitpack_32f: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _32f_)  // non-essential check, but useful for debugging
      error("algo_encode_bitpack_32f: Unknown data format");
#endif

   // Allocate buffer
   void* res = calloc(1, len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_vbr_64d: malloc failed");
#endif

   float* res_arr = (float*)res;

   uint8_t num_bits = *(uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint32_t));

   uint32_t num_bytes = *(uint32_t*)((uint8_t*)(*a_args->src) +
                                     sizeof(uint32_t) + sizeof(uint8_t));

   int b = 0;

   int result_index = 0;

   uint64_t tmp_int = 0;

   int tmp_int_bit_index = 0;

   int res_len = (int)ceil(num_bytes * 8);  // in bits

   for (int i = 0; i < res_len; i++) {
      int value = (tmp_arr[b / 8] & (1 << (b % 8))) != 0;
      if (tmp_int_bit_index == num_bits && result_index * 4 < len) {
         res_arr[result_index] =
             (float)(tmp_int * a_args->scale_factor) / (exp2(num_bits) - 1);
         result_index++;
         tmp_int_bit_index = 0;
      }
      if (value)
         tmp_int |= 1 << tmp_int_bit_index;
      else
         tmp_int &= ~(1 << tmp_int_bit_index);
      tmp_int_bit_index++;
      b++;
   }
   if (tmp_int_bit_index == num_bits && result_index * 4 < len) {
      res_arr[result_index] =
          (float)(tmp_int * a_args->scale_factor) / (exp2(num_bits) - 1);
      result_index++;
      tmp_int_bit_index = 0;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len, a_args->dest, a_args->dest_len);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}

void algo_encode_bitpack_64d(void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 *
 * @param args Pointer to algo_args struct.
 *
 * @return void
 */
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

#ifdef ERROR_CHECK
   if (a_args == NULL)
      error("algo_encode_vbr_64d: args is NULL");
#endif

   // Cast 32-bit to 64-bit

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   // Get array length (in bytes)
   uint32_t len = *(uint32_t*)(*a_args->src) * sizeof(double);

#ifdef ERROR_CHECK
   if (len <= 0)
      error("algo_encode_vbr_64d: len is <= 0");
#endif

#ifdef ERROR_CHECK
   if (a_args->src_format !=
       _64d_)  // non-essential check, but useful for debugging
      error("algo_encode_vbr_64d: Unknown data format");
#endif

   // Allocate buffer
   void* res = calloc(1, len);

#ifdef ERROR_CHECK
   if (res == NULL)
      error("algo_encode_vbr_64d: malloc failed");
#endif

   double* res_arr = (double*)res;

   uint8_t num_bits = *(uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint32_t));

   uint32_t num_bytes = *(uint32_t*)((uint8_t*)(*a_args->src) +
                                     sizeof(uint32_t) + sizeof(uint8_t));

   int b = 0;

   int result_index = 0;

   uint64_t tmp_int = 0;

   int tmp_int_bit_index = 0;

   int res_len = (int)ceil(num_bytes * 8);  // in bits

   for (int i = 0; i < res_len; i++) {
      int value = (tmp_arr[b / 8] & (1 << (b % 8))) != 0;
      if (tmp_int_bit_index == num_bits && result_index * 8 < len) {
         res_arr[result_index] =
             (double)(tmp_int * a_args->scale_factor) / (exp2(num_bits) - 1);
         result_index++;
         tmp_int_bit_index = 0;
      }
      if (value)
         tmp_int |= 1 << tmp_int_bit_index;
      else
         tmp_int &= ~(1 << tmp_int_bit_index);
      tmp_int_bit_index++;
      b++;
   }
   if (tmp_int_bit_index == num_bits && result_index * 8 < len) {
      res_arr[result_index] =
          (double)(tmp_int * a_args->scale_factor) / (exp2(num_bits) - 1);
      result_index++;
      tmp_int_bit_index = 0;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, &res, len, a_args->dest, a_args->dest_len);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}

/*
    @section Algo switch
*/

Algo set_compress_algo(int algo, int accession) {
   switch (algo) {
      case _lossless_:
         return algo_decode_lossless;
      case _log2_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_log_2_transform_32f;
            case _64d_:
               return algo_decode_log_2_transform_64d;
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast32_64d;
            case _32f_:
               return algo_decode_lossless;  // casting 32 to 32 is just
                                             // lossless
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast16_64d;
            case _32f_:
               return algo_decode_cast16_32f;
         }
      };
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta16_transform_32f;
            case _64d_:
               return algo_decode_delta16_transform_64d;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta24_transform_32f;
            case _64d_:
               return algo_decode_delta24_transform_64d;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta32_transform_32f;
            case _64d_:
               return algo_decode_delta32_transform_64d;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta16_transform_32f;
            case _64d_:
               return algo_decode_vdelta16_transform_64d;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta24_transform_32f;
            case _64d_:
               return algo_decode_vdelta24_transform_64d;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vbr_32f;
            case _64d_:
               return algo_decode_vbr_64d;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_decode_bitpack_32f;
            case _64d_:
               return algo_decode_bitpack_64d;
         }
      };
      default:
         error("set_compress_algo: Unknown compression algorithm");
   }
}

Algo set_decompress_algo(int algo, int accession) {
   switch (algo) {
      case _lossless_:
         return algo_encode_lossless;
      case _log2_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_log_2_transform_32f;
            case _64d_:
               return algo_encode_log_2_transform_64d;
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast32_64d;
            case _32f_:
               return algo_encode_lossless;  // casting 32 to 32 is just
                                             // lossless
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast16_64d;
            case _32f_:
               return algo_encode_cast16_32f;
         }
      }
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta16_transform_32f;
            case _64d_:
               return algo_encode_delta16_transform_64d;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta24_transform_32f;
            case _64d_:
               return algo_encode_delta24_transform_64d;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta32_transform_32f;
            case _64d_:
               return algo_encode_delta32_transform_64d;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta16_transform_32f;
            case _64d_:
               return algo_encode_vdelta16_transform_64d;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta24_transform_32f;
            case _64d_:
               return algo_encode_vdelta24_transform_64d;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vbr_32f;
            case _64d_:
               return algo_encode_vbr_64d;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_encode_bitpack_32f;
            case _64d_:
               return algo_encode_bitpack_64d;
         }
      };
      default:
         error("set_decompress_algo: Unknown compression algorithm");
   }
}

int get_algo_type(char* arg) {
   if (arg == NULL)
      error("get_algo_type: arg is NULL");
   if (strcmp(arg, "lossless") == 0 || *arg == '\0' || *arg == "")
      return _lossless_;
   else if (strcmp(arg, "log") == 0)
      return _log2_transform_;
   else if (strcmp(arg, "cast") == 0)
      return _cast_64_to_32_;
   else if (strcmp(arg, "cast16") == 0)
      return _cast_64_to_16_;
   else if (strcmp(arg, "delta16") == 0)
      return _delta16_transform_;
   else if (strcmp(arg, "delta24") == 0)
      return _delta24_transform_;
   else if (strcmp(arg, "delta32") == 0)
      return _delta32_transform_;
   else if (strcmp(arg, "vdelta16") == 0)
      return _vdelta16_transform_;
   else if (strcmp(arg, "vdelta24") == 0)
      return _vdelta24_transform_;
   else if (strcmp(arg, "vbr") == 0)
      return _vbr_;
   else if (strcmp(arg, "bitpack") == 0)
      return _bitpack_;
   else
      error("get_algo_type: Unknown compression algorithm");
}