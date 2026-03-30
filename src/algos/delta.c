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

/*
    @section Encoding functions
*/

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint16_t)) + sizeof(uint16_t) + sizeof(double);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

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
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array
   *a_args->src += (len * sizeof(uint32_t)) + sizeof(uint16_t) + sizeof(double);

   return;
}
