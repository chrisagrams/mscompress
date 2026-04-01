#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mscompress.h"

/*
    @section Decoding functions (compress path)
*/

/**
 * @brief Decode and downcast a 32-bit float array to packed 24-bit unsigned integers.
 *
 * Decodes the source buffer, then scales each 32-bit float by `scale_factor`
 * and stores as a packed 24-bit (3-byte big-endian) unsigned integer, clamping
 * values that exceed 16777215 (UINT24_MAX).
 *
 * Output layout: `[uint16_t len][uint8_t[3] values...]`.
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 */
void algo_decode_cast24_32f(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_cast24_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_cast24_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(float);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t);

   uint8_t* res = calloc(res_len, 1);

   if (res == NULL) {
      error("algo_decode_cast24_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   // Store length header
   memcpy(res, &len, sizeof(uint16_t));

   float* f = (float*)(decoded);
   uint8_t* dest = res + sizeof(uint16_t);

   for (int i = 0; i < len; i++) {
      uint32_t val = (uint32_t)floor(f[i] * a_args->scale_factor);
      if (val > 16777215)
         val = 16777215;
      dest[i * 3]     = (val >> 16) & 0xFF;
      dest[i * 3 + 1] = (val >> 8) & 0xFF;
      dest[i * 3 + 2] = val & 0xFF;
   }

   free(decoded);

   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;
}


/**
 * @brief Decode and downcast a 64-bit double array to packed 24-bit unsigned integers.
 *
 * Decodes the source buffer, then scales each 64-bit double by `scale_factor`
 * and stores as a packed 24-bit (3-byte big-endian) unsigned integer, clamping
 * values that exceed 16777215 (UINT24_MAX).
 *
 * Output layout: `[uint16_t len][uint8_t[3] values...]`.
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 */
void algo_decode_cast24_64d(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_cast24_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_cast24_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(double);

   size_t res_len = (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t);

   uint8_t* res = calloc(res_len, 1);

   if (res == NULL) {
      error("algo_decode_cast24_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   // Store length header
   memcpy(res, &len, sizeof(uint16_t));

   double* f = (double*)(decoded);
   uint8_t* dest = res + sizeof(uint16_t);

   for (int i = 0; i < len; i++) {
      uint32_t val = (uint32_t)floor(f[i] * a_args->scale_factor);
      if (val > 16777215)
         val = 16777215;
      dest[i * 3]     = (val >> 16) & 0xFF;
      dest[i * 3 + 1] = (val >> 8) & 0xFF;
      dest[i * 3 + 2] = val & 0xFF;
   }

   free(decoded);

   *a_args->dest = (char *)res;
   *a_args->dest_len = res_len;
}

/*
    @section Encoding functions (decompress path)
*/

/**
 * @brief Reconstruct a 32-bit float array from packed 24-bit unsigned integer data.
 *
 * Reads packed 24-bit (3-byte big-endian) values, divides each by `scale_factor`
 * to recover the original float, then encodes the result.
 *
 * Input layout: `[uint16_t len][uint8_t[3] values...]`.
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 */
void algo_encode_cast24_32f(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_cast24_32f: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_cast24_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_cast24_32f: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get source array (packed uint24 values after header)
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_cast24_32f: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   for (int i = 0; i < len; i++) {
      uint32_t val = ((uint32_t)arr[i * 3] << 16) |
                     ((uint32_t)arr[i * 3 + 1] << 8) |
                     (uint32_t)arr[i * 3 + 2];
      res[i] = (float)val / a_args->scale_factor;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)&res, res_len,
                   (char *)a_args->dest, a_args->dest_len);

   a_args->out_count = len;

   // Move src pointer
   *a_args->src += (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t);
}


/**
 * @brief Reconstruct a 64-bit double array from packed 24-bit unsigned integer data.
 *
 * Reads packed 24-bit (3-byte big-endian) values, divides each by `scale_factor`
 * to recover the original double, then encodes the result.
 *
 * Input layout: `[uint16_t len][uint8_t[3] values...]`.
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 */
void algo_encode_cast24_64d(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_cast24_64d: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_cast24_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_cast24_64d: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   // Get source array (packed uint24 values after header)
   uint8_t* arr = (uint8_t*)((uint8_t*)(*a_args->src) + sizeof(uint16_t));

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_cast24_64d: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   for (int i = 0; i < len; i++) {
      uint32_t val = ((uint32_t)arr[i * 3] << 16) |
                     ((uint32_t)arr[i * 3 + 1] << 8) |
                     (uint32_t)arr[i * 3 + 2];
      res[i] = (double)val / a_args->scale_factor;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)&res, res_len,
                   (char *)a_args->dest, a_args->dest_len);

   a_args->out_count = len;

   // Move src pointer
   *a_args->src += (len * 3 * sizeof(uint8_t)) + sizeof(uint16_t);
}
