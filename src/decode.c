/**
 * @file decode.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to decode base64 and zlib compressed strings
 * to raw binary. Uses https://github.com/aklomp/base64.git library for base64
 * decoding.
 * @version 0.0.1
 * @date 2021-12-21
 *
 * @copyright
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/zlib/zlib.h"
#include "libbase64.h"
#include "mscompress.h"

/**
 * @brief Allocates a character buffer for base64 decoding output.
 * @param size The number of bytes to allocate.
 * @return A pointer to the allocated buffer. Terminates with an error if allocation fails.
 */
char* base64_alloc(size_t size) {
   char* r;

   r = malloc(sizeof(char) * size);

   if (r == NULL)
      error("base64_alloc: failed to allocate memory.\n");

   return r;
}

/**
 * @brief Decodes a base64-encoded string into a destination buffer.
 * @param src Pointer to the beginning of the base64 string (may be a substring
 *            within a larger buffer; not required to be null-terminated).
 * @param dest Pre-allocated destination buffer for the decoded binary output.
 * @param src_len Length of the base64 string in bytes.
 * @param out_len Pointer to a variable that receives the length of the decoded output.
 */
void decode_base64(char* src, char* dest, size_t src_len, size_t* out_len)
{
   int b64_ret = base64_decode(src, src_len, dest, out_len, 0);

   if (b64_ret == 0)
      error("decode_base64: base64_decode returned with an error. (%d)\n",
            b64_ret);
}

/**
 * @brief Decodes a base64+zlib encoded binary block with a size header prepended.
 *
 * Decodes the base64 string, zlib-decompresses the result, and prepends a
 * `ZLIB_SIZE_OFFSET`-byte header containing the decompressed data length.
 * The decoded binary data in the output starts at `(*dest + ZLIB_SIZE_OFFSET)`.
 *
 * @param z Pointer to an initialized `z_stream` used for zlib decompression.
 * @param src Pointer to the base64-encoded source string.
 * @param src_len Length of the source string in bytes.
 * @param dest Pointer to a char pointer that receives a newly malloc'd buffer.
 *             The first ZLIB_SIZE_OFFSET bytes contain the decompressed length,
 *             followed by the decompressed binary data.
 * @param out_len Pointer to a variable that receives the total output size
 *                `(decompressed data length + ZLIB_SIZE_OFFSET)`.
 * @param tmp Pointer to a data_block_t used as a reusable temporary buffer for
 *            base64 decoding. Will be reallocated if too small.
 * @note The caller is responsible for freeing the buffer returned via `*dest`.
 */
void decode_zlib_fun(z_stream* z, char* src, size_t src_len, char** dest,
                     size_t* out_len, data_block_t* tmp)
{
   if (src == NULL)
      error("decode_zlib_fun: src is NULL.\n");

   if (src_len <= 0)
      error("decode_zlib_fun: src_len is <= 0.\n");

   if (dest == NULL)
      error("decode_zlib_fun: dest is NULL.\n");

   if (out_len == NULL)
      error("decode_zlib_fun: out_len is NULL.\n");

   if (tmp == NULL)
      error("decode_zlib_fun: tmp is NULL.\n");

   if (z == NULL)
      error("decode_zlib_fun: z is NULL.\n");

   size_t b64_out_len = 0;

   // char* b64_out_buff = base64_alloc(sizeof(char) * src_len);

   if (tmp->max_size < src_len)
      realloc_data_block(tmp, src_len);

   char* b64_out_buff = tmp->mem;

   decode_base64(src, b64_out_buff, src_len, &b64_out_len);

   if (b64_out_buff == NULL)
      error("decode_zlib_fun: base64_decode returned with an error.\n");

   zlib_block_t* decmp_output = zlib_alloc(ZLIB_SIZE_OFFSET);

   ZLIB_TYPE decmp_size =
       (ZLIB_TYPE)zlib_decompress(z, b64_out_buff, decmp_output, b64_out_len);

   int zlib_ret = zlib_append_header(decmp_output, &decmp_size, ZLIB_SIZE_OFFSET);
   if (zlib_ret != 0)
      error("decode_zlib_fun: zlib_append_header returned with an error.\n");

   // free(b64_out_buff);

   *out_len = decmp_size + ZLIB_SIZE_OFFSET;

   *dest = (char*)decmp_output->mem;

   free(decmp_output);
}

/**
 * @brief Decodes a base64+zlib encoded binary block without prepending a size header.
 *
 * Decodes the base64 string, zlib-decompresses the result, and stores the
 * raw decompressed data directly in the output buffer (no header prepended).
 *
 * @param z Pointer to an initialized `z_stream` used for zlib decompression.
 * @param src Pointer to the base64-encoded source string.
 * @param src_len Length of the source string in bytes.
 * @param dest Pointer to a char pointer that receives a newly malloc'd buffer
 *             containing the decompressed binary data.
 * @param out_len Pointer to a variable that receives the decompressed data size.
 * @param tmp Pointer to a `data_block_t` used as a reusable temporary buffer for
 *            base64 decoding. Will be reallocated if too small.
 * @note The caller is responsible for freeing the buffer returned via `*dest`.
 */
void decode_zlib_fun_no_header(z_stream* z, char* src, size_t src_len,
                               char** dest, size_t* out_len,
                               data_block_t* tmp) {
   if (src == NULL)
      error("decode_zlib_fun_no_header: src is NULL.\n");

   if (src_len <= 0)
      error("decode_zlib_fun_no_header: src_len is <= 0.\n");

   if (dest == NULL)
      error("decode_zlib_fun_no_header: dest is NULL.\n");

   if (out_len == NULL)
      error("decode_zlib_fun_no_header: out_len is NULL.\n");

   if (z == NULL)
      error("decode_zlib_fun: z is NULL.\n");

   size_t b64_out_len = 0;

   if (tmp->max_size < src_len)
      realloc_data_block(tmp, src_len);

   char* b64_out_buff = tmp->mem;

   decode_base64(src, b64_out_buff, src_len, &b64_out_len);

   if (b64_out_buff == NULL)
      error(
          "decode_zlib_fun_no_header: base64_decode returned with an error.\n");

   zlib_block_t* decmp_output = zlib_alloc(0);

   ZLIB_TYPE decmp_size =
       (ZLIB_TYPE)zlib_decompress(z, b64_out_buff, decmp_output, b64_out_len);

   // free(b64_out_buff);

   *out_len = decmp_size;

   *dest = (char*)decmp_output->mem;

   free(decmp_output);
}

/**
 * @brief Decodes a base64-encoded (but not zlib-compressed) binary block with a
 *        size header prepended.
 *
 * Decodes the base64 string and prepends a `ZLIB_SIZE_OFFSET`-byte header
 * containing the decoded data length. The decoded binary data in the output
 * starts at `(*dest + ZLIB_SIZE_OFFSET)`.
 *
 * @param z Pointer to a `z_stream` (unused, present for function pointer compatibility).
 * @param src Pointer to the base64-encoded source string.
 * @param src_len Length of the source string in bytes.
 * @param dest Pointer to a char pointer that receives a newly malloc'd buffer.
 *             The first `ZLIB_SIZE_OFFSET` bytes contain the decoded length,
 *             followed by the decoded binary data.
 * @param out_len Pointer to a variable that receives the total output size
 *                `(decoded data length + ZLIB_SIZE_OFFSET)`.
 * @param tmp Pointer to a `data_block_t` (unused, present for function pointer compatibility).
 * @note The caller is responsible for freeing the buffer returned via `*dest`.
 */
void decode_no_comp_fun_w_header(z_stream* z, char* src, size_t src_len,
                                 char** dest, size_t* out_len,
                                 data_block_t* tmp)
{
   char* b64_out_buff;

   size_t header;

   b64_out_buff = base64_alloc(src_len + ZLIB_SIZE_OFFSET);

   decode_base64(src, b64_out_buff + ZLIB_SIZE_OFFSET, src_len, out_len);

   header = (ZLIB_TYPE)(*out_len);

   memcpy(b64_out_buff, &header, ZLIB_SIZE_OFFSET);

   *out_len += ZLIB_SIZE_OFFSET;

   *dest = b64_out_buff;
}

/**
 * @brief Decodes a base64-encoded (but not zlib-compressed) binary block without
 *        prepending a size header.
 *
 * Decodes the base64 string and stores the raw decoded data directly in the
 * output buffer (no header prepended).
 *
 * @param z Pointer to a `z_stream` (unused, present for function pointer compatibility).
 * @param src Pointer to the base64-encoded source string.
 * @param src_len Length of the source string in bytes.
 * @param dest Pointer to a char pointer that receives a newly malloc'd buffer
 *             containing the decoded binary data.
 * @param out_len Pointer to a variable that receives the decoded data size.
 * @param tmp Pointer to a `data_block_t` (unused, present for function pointer compatibility).
 * @note The caller is responsible for freeing the buffer returned via *dest.
 */
void decode_no_comp_fun_no_header(z_stream* z, char* src, size_t src_len,
                                  char** dest, size_t* out_len,
                                  data_block_t* tmp) {
   char* b64_out_buff;

   b64_out_buff = base64_alloc(src_len);

   decode_base64(src, b64_out_buff, src_len, out_len);

   *dest = b64_out_buff;
}

/**
 * @brief Sets the decode function based on the compression method, lossy
 * algorithm, and accession type.
 *
 * @param compression_method The compression method used (e.g., `_zlib_`, `_no_comp_`).
 * @param algo The lossy algorithm used (e.g., `_lossless_`, `_cast_64_to_32_`).
 * @param accession The accession type (e.g., `_32f_`).
 *
 * @return A pointer to the appropriate decode function, or `NULL` if an error
 * occurs.
 */
decode_fun set_decode_fun(int compression_method, int algo, int accession)
{
   if (algo == 0) {
      error("set_decode_fun: lossy is 0.\n");
      return NULL;
   }
   switch (compression_method) {
      case _zlib_:
         if (algo == _lossless_ ||
             (algo == _cast_64_to_32_ && accession == _32f_))
            return decode_zlib_fun;
         else
            return decode_zlib_fun_no_header;
      case _no_comp_:
         if (algo == _lossless_ ||
             (algo == _cast_64_to_32_ && accession == _32f_))
            return decode_no_comp_fun_w_header;
         else
            return decode_no_comp_fun_no_header;
      default:
         error("set_decode_fun: Unknown source compression method.\n");
         return NULL;
   }
}
