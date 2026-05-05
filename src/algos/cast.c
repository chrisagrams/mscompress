#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mscompress.h"

/*
    @section Decoding functions
*/

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

/*
    @section Encoding functions
*/

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
   a_args->enc_fun(a_args->z, (char **)&res, len * sizeof(double),
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)&res, len * sizeof(float),
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)&res, len * sizeof(double),
                   (char *)a_args->dest, a_args->dest_len);

   // Move src pointer
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t);

   // // Free buffer
   // free(res);
   return;
}
