#include <float.h>
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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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
   free(diff_arr);

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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
   free(diff_arr);

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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
   free(diff_arr);

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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
   free(diff_arr);

   // Store length of array in first 4 bytes
   memcpy(res, &len, sizeof(uint16_t));

   // Return result
   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;

   return;
}

/*
    @section Encoding functions
*/

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);
   free(res_start);

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);
   free(res_start);

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);
   free(res_start);

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);
   free(res_start);

   // Move to next array
   *a_args->src += (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t) +
                   sizeof(float) + sizeof(float);

   return;
}
