/**
 * @file encode.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to encode raw binary to base64 and zlib
 * compressed strings. Uses https://github.com/aklomp/base64.git library for
 * base64 encoding.
 * @version 0.0.1
 * @date 2021-12-21
 *
 * @copyright
 *
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/zlib/zlib.h"
#include "libbase64.h"
#include "mscompress.h"

/**
 * @brief Encodes a zlib compressed block into a base64 string.
 * @param zblk A `zlib_block_t` struct with `zblk->buff` populated as the source buffer.
 * @param dest Pre-allocated destination buffer for the base64-encoded output.
 * @param src_len Length of the data in `zblk->buff` to encode.
 * @param out_len Pointer to a variable that receives the length of the base64 output.
 * @warning This function frees the `zlib_block_t*` `zblk` parameter internally.
 *          The caller must not free `zblk` after calling this function.
 */
void encode_base64(zlib_block_t* zblk, char* dest, size_t src_len,
                   size_t* out_len)
{
   if (zblk == NULL || zblk->buff == NULL)
      error("encode_base64: zblk is NULL");

   if (dest == NULL)
      error("encode_base64: dest is NULL");

   if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
      error("encode_base64: src_len is invalid");

   if (out_len == NULL)
      error("encode_base64: out_len is NULL");

   // char* b64_out_buff;

   // b64_out_buff = malloc(sizeof(char)*src_len*2);

   base64_encode(zblk->buff, src_len, dest, out_len, 0);

   // zlib_dealloc(zblk);
   free(zblk);

   // return b64_out_buff;
}

/**
 * @brief Encodes raw binary data by zlib-compressing and then base64-encoding it,
 *        without prepending a size header.
 * @param z Pointer to an initialized `z_stream` used for zlib compression.
 * @param src Pointer to a char pointer referencing the source data. Advanced by `src_len` on return.
 * @param src_len Length of the source data to compress.
 * @param dest Pre-allocated destination buffer for the base64-encoded output.
 * @param out_len Pointer to a variable that receives the length of the base64 output.
 * @note The src pointer `(*src)` is advanced by `src_len` after encoding.
 */
void encode_zlib_fun_no_header(z_stream* z, char** src, size_t src_len,
                               char* dest, size_t* out_len) {
   if (src == NULL || *src == NULL)
      error("encode_zlib_fun: src is NULL");

   if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
      error("encode_zlib_fun: src_len is invalid");

   if (dest == NULL)
      error("encode_zlib_fun: dest is NULL");

   if (out_len == NULL)
      error("encode_zlib_fun: out_len is NULL");

   if (z == NULL)
      error("encode_zlib_fun: z is NULL");

   Bytef* zlib_encoded;

   size_t zlib_len = 0;

   zlib_block_t* decmp_input;

   zlib_block_t* cmp_output;

   decmp_input = zlib_alloc(0);
   decmp_input->mem = *src;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;

   cmp_output = zlib_alloc(0);

   // void* decmp_header = zlib_pop_header(decmp_input);

   // uint16_t org_len = *(uint16_t*)decmp_header;

   zlib_len = (size_t)zlib_compress(z, ((Bytef*)*src), cmp_output, src_len);
   if (zlib_len == 0) {
      error("encode_zlib_fun: zlib_compress error\n");
      free(decmp_input);
      free(cmp_output);
      // Continue to move forward
      *src += src_len;
      return;
   }
   // zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET,
   // cmp_output, src_len);

   free(decmp_input);
   // free(decmp_header);

   encode_base64(cmp_output, dest, zlib_len, out_len);

   *src += src_len;
}

/**
 * @brief Encodes binary data that has a `ZLIB_SIZE_OFFSET` header by zlib-compressing
 *        and then base64-encoding it.
 * @param z Pointer to an initialized `z_stream` used for zlib compression.
 * @param src Pointer to a char pointer referencing the source data (with header).
 *            Advanced by `(ZLIB_SIZE_OFFSET + original data length)` on return.
 * @param src_len Length of the source data including the header.
 * @param dest Pre-allocated destination buffer for the base64-encoded output.
 * @param out_len Pointer to a variable that receives the length of the base64 output.
 * @note The source data is expected to have a `ZLIB_SIZE_OFFSET`-byte header containing
 *       the original uncompressed data length. The header is read via `zlib_pop_header()`
 *       and freed internally. The src pointer `(*src)` is advanced past header + data.
 */
void encode_zlib_fun_w_header(z_stream* z, char** src, size_t src_len,
                              char* dest, size_t* out_len) {
   if (src == NULL || *src == NULL)
      error("encode_zlib_fun: src is NULL");

   if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
      error("encode_zlib_fun: src_len is invalid");

   if (dest == NULL)
      error("encode_zlib_fun: dest is NULL");

   if (out_len == NULL)
      error("encode_zlib_fun: out_len is NULL");

   if (z == NULL)
      error("encode_zlib_fun: z is NULL");

   Bytef* zlib_encoded;

   size_t zlib_len = 0;

   zlib_block_t* decmp_input;

   zlib_block_t* cmp_output;

   decmp_input = malloc(sizeof(zlib_block_t));
   decmp_input->offset = ZLIB_SIZE_OFFSET;
   decmp_input->mem = *src;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;
   decmp_input->len = src_len + decmp_input->offset;

   cmp_output = zlib_alloc(0);

   void* decmp_header = zlib_pop_header(decmp_input);

   ZLIB_TYPE org_len = *(ZLIB_TYPE*)decmp_header;

   zlib_len = (size_t)zlib_compress(z, ((Bytef*)*src) + ZLIB_SIZE_OFFSET,
                                    cmp_output, org_len);

   if (zlib_len == 0) {
      error("encode_zlib_fun: zlib_compress error\n");
      free(decmp_input);
      free(decmp_header);
      free(cmp_output);
      // Continue to move forward
      *src += org_len + ZLIB_SIZE_OFFSET;
      return;
   }

   free(decmp_input);
   free(decmp_header);

   encode_base64(cmp_output, dest, zlib_len, out_len);

   *src += (ZLIB_SIZE_OFFSET + org_len);
}

/**
 * @brief Encodes binary data with a `ZLIB_SIZE_OFFSET` header directly to base64
 *        without zlib compression.
 * @param z Pointer to a `z_stream` (unused, present for function pointer compatibility).
 * @param src Pointer to a char pointer referencing the source data (with header).
 *            Advanced by (`ZLIB_SIZE_OFFSET` + original data length) on return.
 * @param src_len Length of the source data including the header.
 * @param dest Pre-allocated destination buffer for the base64-encoded output.
 * @param out_len Pointer to a variable that receives the length of the base64 output.
 * @note The source data header (`ZLIB_SIZE_OFFSET` bytes) stores the original data length.
 *       The header is extracted via `zlib_pop_header()` and freed internally.
 *       The src pointer `(*src)` is advanced past header + data.
 * @note The intermediate `zlib_block_t` wrapping the source data is freed inside
 *       encode_base64().
 */
void encode_no_comp_fun_w_header(z_stream* z, char** src, size_t src_len,
                                 char* dest, size_t* out_len) {
   if (src == NULL || *src == NULL)
      error("encode_zlib_fun: src is NULL");

   if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
      error("encode_zlib_fun: src_len is invalid");

   if (dest == NULL)
      error("encode_zlib_fun: dest is NULL");

   if (out_len == NULL)
      error("encode_zlib_fun: out_len is NULL");

   Bytef* zlib_encoded;

   size_t zlib_len = 0;

   zlib_block_t* decmp_input = malloc(sizeof(zlib_block_t));
   if (decmp_input == NULL)
      error("encode_no_comp_fun: malloc failed");

   decmp_input->mem = *src;
   decmp_input->offset = ZLIB_SIZE_OFFSET;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;

   //TODO: Ensure if we need this free in other functions
   // i.e. encode_zlib_fun_w_header, encode_no_comp_fun_no_header, no_encode_w_header
   void* header = zlib_pop_header(decmp_input);
   ZLIB_TYPE org_len = *(ZLIB_TYPE*)header;
   free(header);

   encode_base64(decmp_input, dest, org_len, out_len);  /* encode_base64 frees decmp_input */

   *src += org_len + ZLIB_SIZE_OFFSET;
}

/**
 * @brief Encodes raw binary data directly to base64 without zlib compression
 *        and without a size header.
 * @param z Pointer to a `z_stream` (unused, present for function pointer compatibility).
 * @param src Pointer to a char pointer referencing the source data.
 * @param src_len Length of the source data to encode.
 * @param dest Pre-allocated destination buffer for the base64-encoded output.
 * @param out_len Pointer to a variable that receives the length of the base64 output.
 * @note The src pointer `(*src)` is NOT advanced by this function.
 * @note The intermediate `zlib_block_t` wrapping the source data is freed inside
 *       `encode_base64()`.
 */
void encode_no_comp_fun_no_header(z_stream* z, char** src, size_t src_len,
                                  char* dest, size_t* out_len) {
   if (src == NULL || *src == NULL)
      error("encode_zlib_fun: src is NULL");

   if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
      error("encode_zlib_fun: src_len is invalid");

   if (dest == NULL)
      error("encode_zlib_fun: dest is NULL");

   if (out_len == NULL)
      error("encode_zlib_fun: out_len is NULL");

   Bytef* zlib_encoded;

   size_t zlib_len = 0;

   zlib_block_t* decmp_input = malloc(sizeof(zlib_block_t));
   if (decmp_input == NULL)
      error("encode_no_comp_fun: malloc failed");

   decmp_input->mem = *src;
   // decmp_input->offset = ZLIB_SIZE_OFFSET;
   decmp_input->offset = 0;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;

   // uint16_t org_len = *(uint16_t*)zlib_pop_header(decmp_input);

   encode_base64(decmp_input, dest, src_len, out_len);

   // *src += org_len + ZLIB_SIZE_OFFSET;
}

/**
 * @brief Copies raw binary data (without any encoding) into the destination buffer,
 *        reading the data length from the `ZLIB_SIZE_OFFSET` header. Used by the Python bindings.
 * @param z Pointer to a `z_stream` (unused, present for function pointer compatibility).
 * @param src Pointer to a char pointer referencing the source data (with header).
 *            Advanced by (`ZLIB_SIZE_OFFSET` + original data length) on return.
 * @param src_len Length of the source data including the header (unused; actual length
 *                is read from the header).
 * @param dest Pre-allocated destination buffer to receive the raw binary data.
 * @param out_len Pointer to a variable that receives the number of bytes copied.
 * @note The source data header (`ZLIB_SIZE_OFFSET` bytes) stores the original data length.
 *       The header is extracted via `zlib_pop_header()` and freed internally.
 *       The src pointer `(*src)` is advanced past header + data.
 */
void no_encode_w_header(z_stream* z, char** src, size_t src_len, char* dest,
                        size_t* out_len)
{
   zlib_block_t* decmp_input = malloc(sizeof(zlib_block_t));
   decmp_input->mem = *src;
   decmp_input->offset = ZLIB_SIZE_OFFSET;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;

   void* header = zlib_pop_header(decmp_input);
   ZLIB_TYPE org_len = *(ZLIB_TYPE*)header;
   free(header);

   *out_len = org_len;

   memcpy(dest, decmp_input->buff, org_len);

   free(decmp_input);
   *src += org_len + ZLIB_SIZE_OFFSET;
}

/**
 * @brief Copies raw binary data verbatim into the destination buffer, without a
 *        length header. Used by the Python no-encode read path for *lossy*
 *        streams.
 *
 * Unlike `no_encode_w_header`, the source here is a standalone buffer produced
 * by a lossy decode algo (e.g. `algo_encode_delta32_transform_64d`): freshly
 * reconstructed samples with no `{ZLIB_TYPE len}{payload}` prefix. We therefore
 * copy `src_len` bytes straight through and do NOT advance `*src` — the algo
 * owns and advances its own cache cursor separately.
 *
 * @param z Unused; present for `encode_fun` signature compatibility.
 * @param src Pointer to the source buffer pointer (the reconstructed samples).
 * @param src_len Number of bytes to copy.
 * @param dest Pre-allocated destination buffer.
 * @param out_len Receives the number of bytes copied (`src_len`).
 */
void no_encode_no_header(z_stream* z, char** src, size_t src_len, char* dest,
                         size_t* out_len)
{
   (void)z;
   memcpy(dest, *src, src_len);
   *out_len = src_len;
}

/**
 * @brief Sets the encode function pointer based on the compression method, algorithm, and accession.
 * @param compression_method The compression method to use (e.g. `_zlib_`, `_no_comp_`, `_no_encode_`).
 * @param algo The algorithm to use (e.g. `_lossless_`, `_cast_64_to_32_`).
 * @param accession The accession to use (e.g. `_32f_`, `_64f_`).
 * @return A pointer to the appropriate encode function based on the provided parameters. If the combination of parameters is invalid, the function will print an error message and return `NULL`.
*/
encode_fun set_encode_fun(int compression_method, int algo, int accession) {
   if (algo == 0)
      error("set_encode_fun: lossy is 0");
   switch (compression_method) {
      case _zlib_:
         if (algo == _lossless_ ||
             (algo == _cast_64_to_32_ && accession == _32f_))
            return encode_zlib_fun_w_header;
         else
            return encode_zlib_fun_no_header;
      case _no_comp_:
         if (algo == _lossless_ ||
             (algo == _cast_64_to_32_ && accession == _32f_))
            return encode_no_comp_fun_w_header;
         else
            return encode_no_comp_fun_no_header;
      case _no_encode_:
         if (algo == _lossless_ ||
             (algo == _cast_64_to_32_ && accession == _32f_))
            return no_encode_w_header;
         else
            assert(0);  // Not yet implemented!
      default:
         error("Invalid compression method.");
         return NULL;
   }
}
