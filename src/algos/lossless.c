#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../mscompress.h"

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
   a_args->dec_fun(a_args->z_inflate, *a_args->src, a_args->src_len, a_args->dest,
                   a_args->dest_len, a_args->tmp);

   /* Lossless, don't touch anything */

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
   a_args->enc_fun(a_args->z, a_args->src, a_args->src_len,
                   (char *)a_args->dest, a_args->dest_len);

   return;
}
