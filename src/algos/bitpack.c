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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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

   free(decoded);

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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

   free(decoded);

   // Return result
   *a_args->dest = (char *)res;
   *a_args->dest_len = header_size + bytes_used;

   return;
}

/*
    @section Encoding functions
*/

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)&res, len, (char *)a_args->dest,
                   a_args->dest_len);
   free(res_start);

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)&res, len, (char *)a_args->dest,
                   a_args->dest_len);
   free(res_start);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}
