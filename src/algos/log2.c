#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mscompress.h"

/*
    @section Decoding functions
*/

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

/*
    @section Encoding functions
*/

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array
   *a_args->src += ((len + 1) * sizeof(uint16_t));

   return;
}
