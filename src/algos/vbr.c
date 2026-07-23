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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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

   free(decoded);

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, &decoded,
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

   free(decoded);

   // Return result
   *a_args->dest = (char *)res;
   *a_args->dest_len =
       sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + bytes_used;

   return;
}

/*
    @section Encoding functions
*/

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)&res, len, (char *)a_args->dest,
                   a_args->dest_len);
   free(res_start);

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
   char* res_start = (char*)res;  /* enc_fun advances res */
   a_args->enc_fun(a_args->z, (char **)&res, len, (char *)a_args->dest,
                   a_args->dest_len);
   free(res_start);

   // Move src pointer
   *a_args->src +=
       sizeof(uint32_t) + sizeof(double) + sizeof(uint32_t) + num_bytes;

   // // Free buffer
   // free(res);
   return;
}
