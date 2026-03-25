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


/**
 * @brief Decode binary data without any lossy transformation (passthrough).
 *
 * Decodes the source buffer using the configured encoding format (e.g., base64+zlib)
 * but applies no further data transformation, preserving the original binary values exactly.
 *
 * @param args Pointer to `algo_args` struct containing source data, decode function, and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_decode_lossless(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_lossless: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, a_args->dest,
                   a_args->dest_len, a_args->tmp);

   /* Lossless, don't touch anything */

   return;
}

/**
 * @brief Decode and downcast a 64-bit double array to a 32-bit float array.
 *
 * Decodes the source buffer, then casts each 64-bit double element to a 32-bit float.
 * 
 * The output array is prefixed with a float header storing the element count.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_cast32_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_cast32_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_cast32_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   float* res;

   len = decoded_len / sizeof(double);
   res = malloc(
       (len + 1) *
       sizeof(float));  // Allocate space for result and leave room for header

   if (res == NULL) {
      error("algo_decode_cast32_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   double* f = (double*)(decoded);
   for (int i = 1; i < len + 1; i++) {
      res[i] = (float)f[i - 1];
   }

   // Store length of array in first 4 bytes
   res[0] = (float)len;

   // Return result
   *a_args->dest = (char *)res;
   *a_args->dest_len = (len + 1) * sizeof(float);

   return;
}


/**
 * @brief Decode and downcast a 32-bit float array to a 16-bit unsigned integer array.
 *
 * Decodes the source buffer, then scales each 32-bit float by `a_args->scale_factor`
 * and casts to `uint16_t`, clamping values that exceed `UINT16_MAX`. 
 * 
 * The output array is prefixed with a `uint16_t` header storing the element count.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_cast16_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_cast16_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_cast16_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   res = calloc(1, (len * sizeof(uint16_t)) +
                       sizeof(uint16_t));  // Allocate space for result and
                                           // leave room for header

   if (res == NULL) {
      error("algo_decode_cast16_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   uint16_t* tmp = res + 1;  // Skip header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = (len * sizeof(uint16_t)) + sizeof(uint16_t);

   return;
}


/**
 * @brief Decode and downcast a 64-bit double array to a 16-bit unsigned integer array.
 *
 * Decodes the source buffer, then scales each 64-bit double by `a_args->scale_factor`
 * and casts to `uint16_t`, clamping values that exceed `UINT16_MAX`. 
 * 
 * The output array is prefixed with a `uint16_t` header storing the element count.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_cast16_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_cast16_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_cast16_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   res = calloc(1, (len * sizeof(uint16_t)) +
                       sizeof(uint16_t));  // Allocate space for result and
                                           // leave room for header

   if (res == NULL) {
      error("algo_decode_cast16_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }
   
   uint16_t* tmp = res + 1;  // Skip header

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = (len * sizeof(uint16_t)) + sizeof(uint16_t);

   return;
}

/**
 * @brief Decode and apply log2 transform to a 32-bit float array, producing `uint16_t` output.
 *
 * Decodes the source buffer of 32-bit floats, then applies `floor(log2(x + 1) * scale_factor)`
 * to each element, storing the result as a `uint16_t array`. The +1 offset avoids `log2(0) = -inf`.
 * 
 * The output array is prefixed with a `uint16_t` header storing the element count.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_log_2_transform_32f(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_log_2_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_log_2_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   size_t res_len = (len + 1) * sizeof(uint16_t);

   // Perform log2 transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for header

   if (res == NULL) {
      error("algo_decode_log_2_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply log2 transform to a 64-bit double array, producing `uint16_t` output.
 *
 * Decodes the source buffer of 64-bit doubles, then applies `floor(log2(x + 1) * scale_factor)`
 * to each element, storing the result as a `uint16_t` array. The +1 offset avoids `log2(0) = -inf`.
 * 
 * The output array is prefixed with a `uint16_t` header storing the element count.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_log_2_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_log_2_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_log_2_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   // Perform log2 transform
   res = malloc(
       (len + 1) *
       sizeof(
           uint16_t));  // Allocate space for result and leave room for header

   if (res == NULL) {
      error("algo_decode_log_2_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = (len + 1) * sizeof(uint16_t);

   return;
}


/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 16-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a `uint16_t`.
 * 
 * The first value is preserved at full 32-bit float precision.
 * 
 * Output layout: `[uint16_t len][float first_val][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta16_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta16_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_delta16_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}


/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 16-bit deltas.
 *
 * Decodes the source buffer of 64-bit doubles, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a `uint16_t`. 
 * 
 * Values exceeding `UINT16_MAX` are clamped. 
 * 
 * The first value is preserved at full 64-bit double precision.
 * 
 * Output layout: `[uint16_t len][double first_val][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta16_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta16_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = malloc(res_len);  // Allocate space for result and leave room for
                           // header and first value

   if (res == NULL) {
      error("algo_decode_delta16_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}


/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 24-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a packed 24-bit (3-byte)
 * unsigned integer in big-endian order. 
 * 
 * Values exceeding 16777215 (`UINT24_MAX`) are clamped.
 * 
 * The first value is preserved at full 32-bit float precision.
 * 
 * Output layout: `[uint16_t len][float first_val][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta24_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta24_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   size_t res_len =
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(res_len, 1);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_delta24_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}


/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 24-bit deltas.
 *
 * Decodes the source buffer of 64-bit doubles, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a packed 24-bit (3-byte)
 * unsigned integer in big-endian order. Values exceeding 16777215 (`UINT24_MAX`) are clamped.
 * 
 * The first value is preserved at full 64-bit double precision.
 * 
 * Output layout: `[uint16_t len][double first_val][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta24_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta24_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = calloc(res_len, 1);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_delta24_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 32-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a `uint32_t`. The first value
 * is preserved at full 32-bit float precision.
 * 
 * Output layout: `[uint16_t len][float first_val][uint32_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta32_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta32_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint32_t* res;


   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_delta32_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 32-bit deltas.
 *
 * Decodes the source buffer of 64-bit doubles, then computes consecutive differences
 * `(f[i] - f[i-1]) * scale_factor`, storing each delta as a `uint32_t`. The first value
 * is preserved at full 64-bit double precision.
 * 
 * Output layout: `[uint16_t len][double first_val][uint32_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta32_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta32_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint32_t* res;

   len = decoded_len / sizeof(double);

   size_t res_len =
       (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(double);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_delta32_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply variable-scale delta encoding to a 32-bit float array, producing 16-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, computes the maximum consecutive difference,
 * and derives a per-spectrum scale factor as `UINT16_MAX / max_diff`. Each delta is then
 * stored as a `uint16_t`. The first value and the computed scale factor are both stored
 * at 32-bit float precision in the header.
 * 
 * Output layout: `[uint16_t len][float first_val][float scale_factor][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_vdelta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_vdelta16_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_vdelta16_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_vdelta16_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply variable-scale delta encoding to a 64-bit double array, producing 16-bit deltas.
 *
 * Decodes the source buffer of 64-bit doubles, computes the maximum consecutive difference,
 * and derives a per-spectrum scale factor as `UINT16_MAX / max_diff`. Each delta is then
 * stored as a `uint16_t`. The first value and the computed scale factor are both stored
 * at 32-bit float precision in the header.
 * 
 * Output layout: `[uint16_t len][float first_val][float scale_factor][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_vdelta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_vdelta16_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_vdelta16_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   size_t res_len = (len * sizeof(uint16_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_vdelta16_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}


/**
 * @brief Decode and apply variable-scale delta encoding to a 32-bit float array, producing 24-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, computes the maximum consecutive difference,
 * and derives a per-spectrum scale factor as `UINT24_MAX / max_diff`. Each delta is stored
 * as a packed 24-bit (3-byte) unsigned integer in big-endian order.
 * 
 * Output layout: `[uint16_t len][float first_val][float scale_factor][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_vdelta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_vdelta24_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_vdelta24_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(float);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_vdelta24_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/**
 * @brief Decode and apply variable-scale delta encoding to a 64-bit double array, producing 24-bit deltas.
 *
 * Decodes the source buffer of 64-bit doubles, computes the maximum consecutive difference,
 * and derives a per-spectrum scale factor as `UINT24_MAX / max_diff`. Each delta is stored
 * as a packed 24-bit (3-byte) unsigned integer in big-endian order.
 * 
 * Output layout: `[uint16_t len][float first_val][float scale_factor][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_vdelta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_vdelta24_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_vdelta24_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint16_t len;
   uint16_t* res;

   len = decoded_len / sizeof(double);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                    sizeof(float) + sizeof(float);

   // Perform delta transform
   res = calloc(1, res_len);  // Allocate space for result and leave room for
                              // header and first value

   if (res == NULL) {
      error("algo_decode_vdelta24_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}


/**
 * @brief Decode and apply variable bit-rate (VBR) compression to a 32-bit float array.
 *
 * Decodes the source buffer of 32-bit floats, determines the base peak intensity and
 * the minimum number of bits needed to represent values at the given threshold
 * (`scale_factor`), then packs each value into a variable-width bitstream.
 * 
 * Output layout: `[uint32_t orig_len][float base_peak][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_vbr_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_vbr_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_vbr_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

   if (decoded_len + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) >
       UINT32_MAX) {
      error("algo_decode_vbr_32f: decoded_len > UINT32_MAX");
      a_args->ret_code = -1;
      return;
   }

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

   if (res == NULL) {
      error("algo_decode_vbr_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len =
       sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t) + bytes_used;

   return;
}

/**
 * @brief Decode and apply variable bit-rate (VBR) compression to a 64-bit double array.
 *
 * Decodes the source buffer of 64-bit doubles, determines the base peak intensity and
 * the minimum number of bits needed to represent values at the given threshold
 * (`scale_factor`), then packs each value into a variable-width bitstream.
 * 
 * Output layout: `[uint32_t orig_len][double base_peak][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_vbr_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_decode_vbr_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_vbr_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

   if (decoded_len + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) >
       UINT32_MAX) {
      error("algo_decode_vbr_64d: decoded_len > UINT32_MAX");
      a_args->ret_code = -1;
      return;
   }

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

   if (res == NULL) {
      error("algo_decode_vbr_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len =
       sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + bytes_used;

   return;
}


/**
 * @brief Decode and apply bit-packing compression to a 32-bit float array.
 *
 * Decodes the source buffer of 32-bit floats, normalizes each value to the range
 * [0, 1] relative to `scale_factor`, then packs each normalized value into a fixed
 * number of bits (currently 27). Expects `a_args->src_format` to be `_32f_`.
 * 
 * Output layout: `[uint32_t len][uint8_t num_bits][uint32_t bytes_used][packed bits...]`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_bitpack_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_decode_bitpack_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_bitpack_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

   size_t header_size = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   if (decoded_len + header_size > UINT32_MAX) {
      error("algo_decode_bitpack_32f: decoded_len > UINT32_MAX");
      a_args->ret_code = -1;
      return;
   }

   len = (uint32_t)(decoded_len / sizeof(float));

   float* f = (float*)(decoded);

   uint8_t num_bits = 27;  // TODO: add as argument

   uint32_t expected_bytes =
       (uint32_t)ceil(len * ((float)num_bits / 8));  // Bytes expected to use

   uint32_t res_len = expected_bytes + header_size;

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

   if (res == NULL) {
      error("algo_decode_bitpack_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = header_size + bytes_used;

   return;
}

/**
 * @brief Decode and apply bit-packing compression to a 64-bit double array.
 *
 * Decodes the source buffer of 64-bit doubles, normalizes each value to the range
 * [0, 1] relative to `scale_factor`, then packs each normalized value into a fixed
 * number of bits (currently 27). Expects `a_args->src_format` to be `_64d_`.
 * 
 * Output layout: `[uint32_t len][uint8_t num_bits][uint32_t bytes_used][packed bits...]`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 */
void algo_decode_bitpack_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_decode_bitpack_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_bitpack_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   // Deternmine length of data based on data format
   uint32_t len;
   unsigned char* res;

   size_t header_size = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   if (decoded_len + header_size > UINT32_MAX) {
      error("algo_decode_vbr_64d: decoded_len > UINT32_MAX");
      a_args->ret_code = -1;
      return;
   }

   len = (uint32_t)(decoded_len / sizeof(double));

   double* f = (double*)(decoded);

   uint8_t num_bits = 27;  // TODO: add as argument

   uint32_t expected_bytes =
       (uint32_t)ceil(len * ((double)num_bits / 8));  // Bytes expected to use

   uint32_t res_len = expected_bytes + header_size;

   res =
       calloc(1,
              res_len);  // Allocate space for result and leave room for header

   if (res == NULL) {
      error("algo_decode_bitpack_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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
   *a_args->dest = (char *)res;
   *a_args->dest_len = header_size + bytes_used;

   return;
}

/*
    @section Encoding functions
*/

/**
 * @brief Encode binary data without any lossy transformation (passthrough).
 *
 * Encodes the source buffer using the configured encoding format (e.g., zlib+base64)
 * but applies no data transformation, preserving the original binary values exactly.
 *
 * @param args Pointer to `algo_args` struct containing source data and encoding parameters.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_lossless(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_lossless: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   /* Lossless, don't touch anything */

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, a_args->src, a_args->src_len, a_args->dest,
                   a_args->dest_len);

   return;
}

/**
 * @brief Reconstruct a 64-bit double array from a 32-bit float compressed representation.
 *
 * Reads a float array with a length header, upcasts each 32-bit float to a 64-bit double,
 * then encodes the result. Advances the source pointer past the consumed data.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_cast32_64d(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_cast32: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_cast32_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   float* arr = (float*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

   if (len <= 0) {
      error("algo_encode_cast32_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = malloc(sizeof(double) * len);

   if (res == NULL) {
      error("algo_encode_cast32_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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


/**
 * @brief Reconstruct a 32-bit float array from a 16-bit unsigned integer compressed representation.
 *
 * Reads a uint16_t array with a length header, divides each value by `scale_factor`
 * to recover the original float, then encodes the result. Advances the source pointer
 * past the consumed data. 
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_cast16_32f(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_cast16_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_cast16_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Cast 16-bit to 32-bit

   // Get source array
   uint16_t* arr = (uint16_t*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

   if (len <= 0) {
      error("algo_encode_cast16_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = malloc(sizeof(float) * len);

   if (res == NULL) {
      error("algo_encode_cast16_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   // Get pointer to the array, ignoring the header
   uint16_t* tmp = arr + 1;

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

/**
 * @brief Reconstruct a 64-bit double array from a 16-bit unsigned integer compressed representation.
 *
 * Reads a `uint16_t` array with a length header, divides each value by `scale_factor`
 * to recover the original double, then encodes the result. Advances the source pointer
 * past the consumed data. 
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_cast16_64d(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_cast16_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_cast16_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   uint16_t* arr = (uint16_t*)(*a_args->src);

   // Get array length
   uint16_t len = (uint16_t)arr[0];

   if (len <= 0) {
      error("algo_encode_cast16_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = malloc(sizeof(double) * len);

   if (res == NULL) {
      error("algo_encode_cast16_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from log2-transformed `uint16_t` data.
 *
 * Reads a `uint16_t` array with a length header, applies the inverse transform
 * `exp2(value / scale_factor) - 1` to recover each original float, then encodes
 * the result. Advances the source pointer past the consumed data.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_log_2_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_log_2_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_log_2_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_log_2_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_log_2_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from log2-transformed `uint16_t` data.
 *
 * Reads a `uint16_t` array with a length header, applies the inverse transform
 * `exp2(value / scale_factor) - 1` to recover each original double, then encodes
 * the result. Advances the source pointer past the consumed data.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_log_2_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_log_2_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_log_2_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_log_2_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

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

/**
 * @brief Reconstruct a 32-bit float array from 16-bit delta-encoded data.
 *
 * Reads the starting value and `uint16_t` deltas, reconstructs the original float array
 * by cumulative addition of `delta / scale_factor`, then encodes the result.
 * 
 * Input layout: `[uint16_t len][float start][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint16_t* arr =
       (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from 16-bit delta-encoded data.
 *
 * Reads the starting value and `uint16_t` deltas, reconstructs the original double array
 * by cumulative addition of `delta / scale_factor`, then encodes the result.
 * 
 * Input layout: `[uint16_t len][double start][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(double));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from variable-scale 16-bit delta-encoded data.
 *
 * Reads the starting value, per-spectrum scale factor, and `uint16_t` deltas, then
 * reconstructs the original float array by cumulative addition of `delta / scale_factor`.
 * 
 * Input layout: `[uint16_t len][float start][float scale_factor][uint16_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vdelta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vdelta16_transform_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_vdelta16_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vdelta16_transform_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(float) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_vdelta16_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from variable-scale 16-bit delta-encoded data.
 *
 * Reads the starting value, per-spectrum scale factor, and `uint16_t` deltas, then
 * reconstructs the original double array by cumulative addition of `delta / scale_factor`.
 * 
 * Input layout: `[uint16_t len][float start][float scale_factor][uint16_t deltas...]`.
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vdelta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vdelta16_transform_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_vdelta16_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }


   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vdelta16_transform_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint16_t* arr = (uint16_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(float) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_vdelta16_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from variable-scale 24-bit delta-encoded data.
 *
 * Reads the starting value, per-spectrum scale factor, and packed 24-bit deltas,
 * then reconstructs the original float array by cumulative addition of `delta / scale_factor`.
 * 
 * Input layout: `[uint16_t len][float start][float scale_factor][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vdelta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vdelta24_transform_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_vdelta24_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vdelta24_transform_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                             sizeof(float) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_vdelta24_transform_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from variable-scale 24-bit delta-encoded data.
 *
 * Reads the starting value, per-spectrum scale factor, and packed 24-bit deltas,
 * then reconstructs the original double array by cumulative addition of `delta / scale_factor`.
 * 
 * Input layout: `[uint16_t len][float start][float scale_factor][uint8_t[3] deltas...]`.
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vdelta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vdelta24_transform_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_vdelta24_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vdelta24_transform_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get scale factor

   float scale_factor =
       *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Get source array
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                             sizeof(float) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_vdelta24_transform_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from 24-bit delta-encoded data.
 *
 * Reads the starting value and packed 24-bit (3-byte big-endian) deltas, reconstructs
 * the original float array by cumulative addition of `delta / scale_factor`, then encodes.
 * 
 * Input layout: `[uint16_t len][float start][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }
   
   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint8_t* arr =
       (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from 24-bit delta-encoded data.
 *
 * Reads the starting value and packed 24-bit (3-byte big-endian) deltas, reconstructs
 * the original double array by cumulative addition of `delta / scale_factor`, then encodes.
 * 
 * Input layout: `[uint16_t len][double start][uint8_t[3] deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint8_t* arr =
       (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(double));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from 32-bit delta-encoded data.
 *
 * Reads the starting value and uint32_t deltas, reconstructs the original float array
 * by cumulative addition of `delta / scale_factor`, then encodes the result.
 * 
 * Input layout: `[uint16_t len][float start][uint32_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   float start = *(float*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint32_t* arr =
       (uint32_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) + sizeof(float));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from 32-bit delta-encoded data.
 *
 * Reads the starting value and `uint32_t` deltas, reconstructs the original double array
 * by cumulative addition of `delta / scale_factor`, then encodes the result.
 * 
 * Input layout: `[uint16_t len][double start][uint32_t deltas...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get starting value
   double start = *(double*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Get source array
   uint32_t* arr = (uint32_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t) +
                               sizeof(double));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from VBR-compressed bitstream data.
 *
 * Reads the original length, base peak intensity, and byte count from the header,
 * then unpacks the variable-width bitstream to reconstruct each float as
 * `(packed_value * base_peak) / (2^num_bits - 1)`.
 * 
 * Input layout: `[uint32_t orig_len][float base_peak][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vbr_32f(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vbr_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_vbr_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(float) + sizeof(uint32_t);

   // Get array length
   uint32_t len = *(uint32_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vbr_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = calloc(1, len);

   if (res == NULL) {
      error("algo_encode_vbr_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from VBR-compressed bitstream data.
 *
 * Reads the original length, base peak intensity, and byte count from the header,
 * then unpacks the variable-width bitstream to reconstruct each double as
 * `(packed_value * base_peak) / (2^num_bits - 1)`.
 * 
 * Input layout: `[uint32_t orig_len][double base_peak][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_vbr_64d(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_vbr_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_vbr_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t);

   // Get array length
   uint32_t len = *(uint32_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_vbr_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = calloc(1, len);

   if (res == NULL) {
      error("algo_encode_vbr_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 32-bit float array from bit-packed compressed data.
 *
 * Reads the element count, bit width, and byte count from the header, then unpacks
 * the fixed-width bitstream to reconstruct each float as
 * `(packed_value * scale_factor) / (2^num_bits - 1)`.
 * 
 * Input layout: `[uint32_t len][uint8_t num_bits][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_bitpack_32f(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_bitpack_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_bitpack_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   // Get array length (in bytes)
   uint32_t len = *(uint32_t*)(*a_args->src) * sizeof(float);

   if (len <= 0) {
      error("algo_encode_bitpack_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = calloc(1, len);

   if (res == NULL) {
      error("algo_encode_bitpack_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Reconstruct a 64-bit double array from bit-packed compressed data.
 *
 * Reads the element count, bit width, and byte count from the header, then unpacks
 * the fixed-width bitstream to reconstruct each double as
 * `(packed_value * scale_factor) / (2^num_bits - 1)`.
 * 
 * Input layout: `[uint32_t len][uint8_t num_bits][uint32_t bytes_used][packed bits...]`.
 * 
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_bitpack_64d(void* args)
{
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_bitpack_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_bitpack_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get source array
   unsigned char* arr = (unsigned char*)(*a_args->src);

   unsigned char* tmp_arr =
       arr + sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);

   // Get array length (in bytes)
   uint32_t len = *(uint32_t*)(*a_args->src) * sizeof(double);

   if (len <= 0) {
      error("algo_encode_bitpack_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Allocate buffer
   void* res = calloc(1, len);

   if (res == NULL) {
      error("algo_encode_bitpack_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

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

/**
 * @brief Returns the appropriate compression algorithm function pointer based on the provided algorithm and accession type.
 * @param algo The compression algorithm type.
 * @param accession The data type accession (e.g., `32f` for 32-bit float, `64d` for 64-bit double).
 * @return A function pointer to the corresponding compression algorithm. If the algorithm or accession type is unknown, it returns `NULL` and logs an error.
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
         return NULL;
   }
}

/**
 * @brief Returns the appropriate decompression algorithm function pointer based on the provided algorithm and accession type.
 * @param algo The compression algorithm type.
 * @param accession The data type accession (e.g., `32f` for 32-bit float, `64d` for 64-bit double).
 * @return A function pointer to the corresponding decompression algorithm. If the algorithm or accession type is unknown, it returns `NULL` and logs an error.
 */
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
         return NULL;
   }
}

/**
 * @brief Returns the algorithm type based on the provided argument.
 * @param arg The argument representing the algorithm type.
 * @return An integer representing the algorithm type. If the argument is `NULL` or unknown, it logs an error and returns -1.
 */
int get_algo_type(const char* arg) {
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
   else {
      error("get_algo_type: Unknown compression algorithm");
      return -1;
   }
}