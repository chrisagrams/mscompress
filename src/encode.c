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

void encode_base64(zlib_block_t* zblk, char* dest, size_t src_len,
                   size_t* out_len)
/**
 * @brief Takes a zlib compressed block (zlib_block_t) and returns a base64
 * encoded string.
 *
 * @param zblk A zlib_block_t struct with zblk->buff populated.
 *
 * @param src_len Length of original zlib output string.
 *
 * @param out_len Return by reference of output string length.
 *
 * @return A base64 string.
 *
 */
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

void encode_zlib_fun_no_header(z_stream* z, char** src, size_t src_len,
                               char* dest, size_t* out_len) {
   // assert(0); // this is broken now, need to fix
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
   // zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET,
   // cmp_output, src_len);

   free(decmp_input);
   // free(decmp_header);

   encode_base64(cmp_output, dest, zlib_len, out_len);

   *src += src_len;
}

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
   // zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET,
   // cmp_output, src_len);

   free(decmp_input);
   free(decmp_header);

   encode_base64(cmp_output, dest, zlib_len, out_len);

   *src += (ZLIB_SIZE_OFFSET + org_len);
}

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

   ZLIB_TYPE org_len = *(ZLIB_TYPE*)zlib_pop_header(decmp_input);

   encode_base64(decmp_input, dest, org_len, out_len);

   *src += org_len + ZLIB_SIZE_OFFSET;
}

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

void no_encode_w_header(z_stream* z, char** src, size_t src_len, char* dest,
                        size_t* out_len)
/*
    Performs no encoding (just return the float array), for Python library
*/
{
   zlib_block_t* decmp_input = malloc(sizeof(zlib_block_t));
   decmp_input->mem = *src;
   decmp_input->offset = ZLIB_SIZE_OFFSET;
   decmp_input->buff = decmp_input->mem + decmp_input->offset;

   ZLIB_TYPE org_len = *(ZLIB_TYPE*)zlib_pop_header(decmp_input);

   *out_len = org_len;

   memcpy(dest, decmp_input->buff, org_len);

   *src += org_len + ZLIB_SIZE_OFFSET;
}

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
