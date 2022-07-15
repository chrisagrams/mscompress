/**
 * @file encode.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to encode raw binary to base64 and zlib compressed strings. 
 *        Uses https://github.com/aklomp/base64.git library for base64 encoding.
 * @version 0.0.1
 * @date 2021-12-21
 * 
 * @copyright 
 * 
 */

#include "mscompress.h"
#include "vendor/base64/include/libbase64.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>


char*
encode_base64(zlib_block_t* zblk, size_t src_len, size_t* out_len)
/**
 * @brief Takes a zlib compressed block (zlib_block_t) and returns a base64 encoded string.
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
    char* b64_out_buff;

    b64_out_buff = (char*)malloc(sizeof(char)*src_len*2);

    base64_encode(zblk->buff, src_len, b64_out_buff, out_len, 0);

    free(zblk);

    return b64_out_buff;
}

char*
encode_binary(char** src, size_t* out_len)
/**
 * @brief Takes in a raw binary string, returns a zlib compressed and base64 encoded string.
 * 
 * @param src Pointer to source string. src pointer will point to the end of the string on return.
 * 
 * @param out_len Return by reference of output string length.
 * 
 * @return A zlib compressed and base64 encoded string.
 * 
 */
{
    Bytef* zlib_encoded;

    size_t zlib_len = 0;
    
    zlib_block_t* decmp_input;

    zlib_block_t* cmp_output;

    decmp_input = zlib_alloc(ZLIB_SIZE_OFFSET);
    decmp_input->mem = *src;

    cmp_output = zlib_alloc(0);

    void* decmp_header = zlib_pop_header(decmp_input);

    uint16_t org_len = *(uint16_t*)decmp_header;

    zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET, cmp_output, org_len);

    *src += (ZLIB_SIZE_OFFSET + org_len);

    free(decmp_input);
    free(decmp_header);

    return encode_base64(cmp_output, zlib_len, out_len);
}