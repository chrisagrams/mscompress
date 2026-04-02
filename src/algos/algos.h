#ifndef ALGOS_H
#define ALGOS_H

#include <math.h>
#include <stdint.h>

/**
 * @brief Pack float values into 24-bit big-endian unsigned integers.
 *
 * Scales each value by scale_factor, clamps to UINT24_MAX (16777215),
 * and writes 3 bytes per element into dest. If indices is non-NULL,
 * gathers from arr[indices[i]]; otherwise reads sequentially.
 */
static inline void pack_uint24_32f(const float* arr, const int* indices,
                                   int count, double scale_factor,
                                   uint8_t* dest) {
   for (int i = 0; i < count; i++) {
      int src_idx = indices ? indices[i] : i;
      uint32_t val = (uint32_t)floor(arr[src_idx] * scale_factor);
      if (val > 16777215) val = 16777215;
      dest[i * 3]     = (val >> 16) & 0xFF;
      dest[i * 3 + 1] = (val >> 8) & 0xFF;
      dest[i * 3 + 2] = val & 0xFF;
   }
}

/**
 * @brief Pack double values into 24-bit big-endian unsigned integers.
 *
 * Same as pack_uint24_32f but for double-precision source data.
 */
static inline void pack_uint24_64d(const double* arr, const int* indices,
                                   int count, double scale_factor,
                                   uint8_t* dest) {
   for (int i = 0; i < count; i++) {
      int src_idx = indices ? indices[i] : i;
      uint32_t val = (uint32_t)floor(arr[src_idx] * scale_factor);
      if (val > 16777215) val = 16777215;
      dest[i * 3]     = (val >> 16) & 0xFF;
      dest[i * 3 + 1] = (val >> 8) & 0xFF;
      dest[i * 3 + 2] = val & 0xFF;
   }
}

/* lossless.c */
void algo_decode_lossless(void* args);
void algo_encode_lossless(void* args);

/* cast.c */
void algo_decode_cast32_64d(void* args);
void algo_decode_cast16_32f(void* args);
void algo_decode_cast16_64d(void* args);
void algo_encode_cast32_64d(void* args);
void algo_encode_cast16_32f(void* args);
void algo_encode_cast16_64d(void* args);

/* log2.c */
void algo_decode_log_2_transform_32f(void* args);
void algo_decode_log_2_transform_64d(void* args);
void algo_encode_log_2_transform_32f(void* args);
void algo_encode_log_2_transform_64d(void* args);

/* delta.c */
void algo_decode_delta16_transform_32f(void* args);
void algo_decode_delta16_transform_64d(void* args);
void algo_decode_delta24_transform_32f(void* args);
void algo_decode_delta24_transform_64d(void* args);
void algo_decode_delta32_transform_32f(void* args);
void algo_decode_delta32_transform_64d(void* args);
void algo_encode_delta16_transform_32f(void* args);
void algo_encode_delta16_transform_64d(void* args);
void algo_encode_delta24_transform_32f(void* args);
void algo_encode_delta24_transform_64d(void* args);
void algo_encode_delta32_transform_32f(void* args);
void algo_encode_delta32_transform_64d(void* args);

/* vdelta.c */
void algo_decode_vdelta16_transform_32f(void* args);
void algo_decode_vdelta16_transform_64d(void* args);
void algo_decode_vdelta24_transform_32f(void* args);
void algo_decode_vdelta24_transform_64d(void* args);
void algo_encode_vdelta16_transform_32f(void* args);
void algo_encode_vdelta16_transform_64d(void* args);
void algo_encode_vdelta24_transform_32f(void* args);
void algo_encode_vdelta24_transform_64d(void* args);

/* vbr.c */
void algo_decode_vbr_32f(void* args);
void algo_decode_vbr_64d(void* args);
void algo_encode_vbr_32f(void* args);
void algo_encode_vbr_64d(void* args);

/* cast24.c */
void algo_decode_cast24_32f(void* args);
void algo_decode_cast24_64d(void* args);
void algo_encode_cast24_32f(void* args);
void algo_encode_cast24_64d(void* args);

/* topn.c */
void algo_decode_topn_32f(void* args);
void algo_decode_topn_64d(void* args);

/* bitpack.c */
void algo_decode_bitpack_32f(void* args);
void algo_decode_bitpack_64d(void* args);
void algo_encode_bitpack_32f(void* args);
void algo_encode_bitpack_64d(void* args);

#endif /* ALGOS_H */
