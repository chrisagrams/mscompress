#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mscompress.h"
#include "algos.h"

/*
    @section Helper: argsort descending by value, tiebreak ascending by index
*/

typedef struct {
   int index;
   double value;
} indexed_value_t;

/**
 * @brief qsort comparator: sort indexed_value_t by value descending.
 *        Ties are broken by index ascending for deterministic ordering.
 */
static int cmp_descending(const void* a, const void* b) {
   const indexed_value_t* ia = (const indexed_value_t*)a;
   const indexed_value_t* ib = (const indexed_value_t*)b;
   if (ia->value > ib->value) return -1;
   if (ia->value < ib->value) return 1;
   /* tiebreak: ascending by index for deterministic results */
   if (ia->index < ib->index) return -1;
   if (ia->index > ib->index) return 1;
   return 0;
}

/**
 * @brief qsort comparator: sort indexed_value_t by index ascending.
 *        Used to restore original positional order after a value-based sort.
 */
static int cmp_ascending_index(const void* a, const void* b) {
   return ((const indexed_value_t*)a)->index -
          ((const indexed_value_t*)b)->index;
}

/**
 * @brief Compute top-N indices from a double array.
 *
 * Allocates and returns an array of `*out_n` indices (sorted ascending)
 * representing the top-N values. Caller must free the returned array.
 *
 * @param values  Array of values to rank.
 * @param len     Number of elements.
 * @param n       Maximum peaks to keep.
 * @param out_n   Output: actual count kept (min(len, n)).
 * @return Allocated array of indices, or NULL on error.
 */
static int* compute_topn_indices(const double* values, int len, int n,
                                 int* out_n) {
   int keep = (len <= n) ? len : n;
   *out_n = keep;

   indexed_value_t* iv = malloc(len * sizeof(indexed_value_t));
   if (iv == NULL) return NULL;

   for (int i = 0; i < len; i++) {
      iv[i].index = i;
      iv[i].value = values[i];
   }

   /* Sort descending by value */
   qsort(iv, len, sizeof(indexed_value_t), cmp_descending);

   /* Re-sort top-N by ascending index to preserve original order */
   qsort(iv, keep, sizeof(indexed_value_t), cmp_ascending_index);

   int* indices = malloc(keep * sizeof(int));
   if (indices == NULL) {
      free(iv);
      return NULL;
   }
   for (int i = 0; i < keep; i++)
      indices[i] = iv[i].index;

   free(iv);
   return indices;
}

/*
    @section Decoding functions (compress path)
*/

/**
 * @brief Top-N peak filter + uint24 quantization for 32-bit float arrays.
 *
 * If peer_src is non-NULL (m/z side, coupled): decodes own m/z + peer
 * intensity, filters to top-N by intensity.
 * If peer_src is NULL (intensity side): filters own values to top-N.
 * Packs surviving values as uint24.
 *
 * Output layout: `[uint16_t count][uint8_t[3] values...]`.
 *
 * @param args Pointer to `algo_args` struct.
 */
void algo_decode_topn_32f(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_topn_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_topn_32f: Unknown data format. Expected _32f_, got %d",
            a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   /* Decode own data */
   char* decoded = NULL;
   size_t decoded_len = 0;
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   float* arr = (float*)decoded;
   int len = decoded_len / sizeof(float);

   int N = a_args->topn;
   int keep = len;
   int* indices = NULL;

   if (len > N) {
      /* Build double array for ranking */
      double* rank_values = malloc(len * sizeof(double));
      if (rank_values == NULL) {
         error("algo_decode_topn_32f: malloc failed");
         free(decoded);
         a_args->ret_code = -1;
         return;
      }

      if (a_args->peer_src != NULL) {
         /* Coupled (m/z side): decode peer intensity for ranking */
         char* peer_decoded = NULL;
         size_t peer_decoded_len = 0;
         a_args->peer_dec_fun(a_args->z, a_args->peer_src,
                              a_args->peer_src_len, &peer_decoded,
                              &peer_decoded_len, a_args->tmp);

         int peer_len = peer_decoded_len / sizeof(float);
         float* peer_arr = (float*)peer_decoded;

         /* Use min(len, peer_len) for safety */
         int rank_len = (len < peer_len) ? len : peer_len;
         for (int i = 0; i < rank_len; i++)
            rank_values[i] = (double)peer_arr[i];
         for (int i = rank_len; i < len; i++)
            rank_values[i] = 0.0;

         free(peer_decoded);
      } else {
         /* Uncoupled (intensity side): rank by own values */
         for (int i = 0; i < len; i++)
            rank_values[i] = (double)arr[i];
      }

      indices = compute_topn_indices(rank_values, len, N, &keep);
      free(rank_values);

      if (indices == NULL) {
         error("algo_decode_topn_32f: compute_topn_indices failed");
         free(decoded);
         a_args->ret_code = -1;
         return;
      }
   }

   /* Pack as uint24: [uint16_t count][uint8_t[3] * count] */
   size_t res_len = sizeof(uint16_t) + (keep * 3 * sizeof(uint8_t));
   uint8_t* res = calloc(res_len, 1);
   if (res == NULL) {
      error("algo_decode_topn_32f: malloc failed");
      free(decoded);
      free(indices);
      a_args->ret_code = -1;
      return;
   }

   uint16_t count = (uint16_t)keep;
   memcpy(res, &count, sizeof(uint16_t));
   uint8_t* dest = res + sizeof(uint16_t);

   pack_uint24_32f(arr, indices, keep, a_args->scale_factor, dest);

   free(decoded);
   free(indices);

   *a_args->dest = (char*)res;
   *a_args->dest_len = res_len;
}


/**
 * @brief Top-N peak filter + uint24 quantization for 64-bit double arrays.
 *
 * Same logic as algo_decode_topn_32f but for 64-bit double source data.
 *
 * Output layout: `[uint16_t count][uint8_t[3] values...]`.
 *
 * @param args Pointer to `algo_args` struct.
 */
void algo_decode_topn_64d(void* args) {
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_topn_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_topn_64d: Unknown data format. Expected _64d_, got %d",
            a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   /* Decode own data */
   char* decoded = NULL;
   size_t decoded_len = 0;
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   double* arr = (double*)decoded;
   int len = decoded_len / sizeof(double);

   int N = a_args->topn;
   int keep = len;
   int* indices = NULL;

   if (len > N) {
      double* rank_values = malloc(len * sizeof(double));
      if (rank_values == NULL) {
         error("algo_decode_topn_64d: malloc failed");
         free(decoded);
         a_args->ret_code = -1;
         return;
      }

      if (a_args->peer_src != NULL) {
         /* Coupled (m/z side): decode peer intensity for ranking */
         char* peer_decoded = NULL;
         size_t peer_decoded_len = 0;
         a_args->peer_dec_fun(a_args->z, a_args->peer_src,
                              a_args->peer_src_len, &peer_decoded,
                              &peer_decoded_len, a_args->tmp);

         int peer_elem_size = (a_args->peer_src_format == _64d_)
                                  ? sizeof(double)
                                  : sizeof(float);
         int peer_len = peer_decoded_len / peer_elem_size;

         int rank_len = (len < peer_len) ? len : peer_len;

         if (a_args->peer_src_format == _64d_) {
            double* peer_arr = (double*)peer_decoded;
            for (int i = 0; i < rank_len; i++)
               rank_values[i] = peer_arr[i];
         } else {
            float* peer_arr = (float*)peer_decoded;
            for (int i = 0; i < rank_len; i++)
               rank_values[i] = (double)peer_arr[i];
         }
         for (int i = rank_len; i < len; i++)
            rank_values[i] = 0.0;

         free(peer_decoded);
      } else {
         /* Uncoupled (intensity side): rank by own values */
         for (int i = 0; i < len; i++)
            rank_values[i] = arr[i];
      }

      indices = compute_topn_indices(rank_values, len, N, &keep);
      free(rank_values);

      if (indices == NULL) {
         error("algo_decode_topn_64d: compute_topn_indices failed");
         free(decoded);
         a_args->ret_code = -1;
         return;
      }
   }

   /* Pack as uint24: [uint16_t count][uint8_t[3] * count] */
   size_t res_len = sizeof(uint16_t) + (keep * 3 * sizeof(uint8_t));
   uint8_t* res = calloc(res_len, 1);
   if (res == NULL) {
      error("algo_decode_topn_64d: malloc failed");
      free(decoded);
      free(indices);
      a_args->ret_code = -1;
      return;
   }

   uint16_t count = (uint16_t)keep;
   memcpy(res, &count, sizeof(uint16_t));
   uint8_t* dest = res + sizeof(uint16_t);

   pack_uint24_64d(arr, indices, keep, a_args->scale_factor, dest);

   free(decoded);
   free(indices);

   *a_args->dest = (char*)res;
   *a_args->dest_len = res_len;
}
