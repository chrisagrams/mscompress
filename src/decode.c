/**
 * @file decode.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to decode base64 and zlib compressed strings to raw binary. 
 *        Uses https://github.com/aklomp/base64.git library for base64 decoding.
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
#include "vendor/zlib/zlib.h"


char*
base64_alloc(size_t size)
{
    char* r;

    r = (char*)malloc(sizeof(char) * size);
    
    if(!r)
    {
        fprintf(stderr, "malloc failure in base64_alloc.\n");
        exit(-1);
    }
    
    return r;
}

char*
decode_base64(char* src, char* buff, size_t src_len, size_t* out_len)
/**
 * @brief Wrapper function for base64 libary. 
 * Allocates memory for base64_decode function call and takes care of errors.
 * 
 * @param src Pointer to beginning of base64 string (can be a substring within another string).
 * 
 * @param src_len Length of base64 string (not NULL terminated).
 * 
 * @param out_len Pass-by-reference return value of length of decoded string.
 * 
 * @return Decoded string.
 */
{
    int b64_ret;

    char* b64_out_buff;

    // b64_out_buff = (char*)malloc(sizeof(char) * src_len);

    b64_ret = base64_decode(src, src_len, buff, out_len, 0);

    if(b64_ret == 0) 
    {
        // free(b64_out_buff);
        return NULL;
    }

    return b64_out_buff;
}


char*
zlib_fun(char* input_map, int start_position, int end_position, size_t* out_len)
{
    int zlib_ret;

    size_t zlib_out_len = 0;
    size_t b64_out_len = 0;

    char* b64_out_buff;
    zlib_block_t* decmp_output;

    uint16_t decmp_size;

    size_t len = end_position - start_position;

    b64_out_buff = base64_alloc(sizeof(char) * len);

    decode_base64(input_map + start_position, b64_out_buff, len, &b64_out_len);
    
    if (!b64_out_buff)
        return NULL;

    decmp_output = zlib_alloc(ZLIB_SIZE_OFFSET);

    decmp_size = (uint16_t)zlib_decompress(b64_out_buff, decmp_output, b64_out_len);

    zlib_append_header(decmp_output, &decmp_size, ZLIB_SIZE_OFFSET);

    free(b64_out_buff);
    
    *out_len = decmp_size + ZLIB_SIZE_OFFSET;
    
    Bytef* r = decmp_output->mem;

    free(decmp_output);

    return r;
}

char*
no_comp_fun(char* input_map, int start_position, int end_position, size_t* out_len)
{

    char* b64_out_buff;

    size_t header;

    size_t len = end_position - start_position;

    b64_out_buff = base64_alloc(sizeof(char) * len + ZLIB_SIZE_OFFSET);

    decode_base64(input_map + start_position, b64_out_buff + ZLIB_SIZE_OFFSET, len, out_len);

    header = (uint16_t)(*out_len);

    memcpy(b64_out_buff, &header, ZLIB_SIZE_OFFSET);

    *out_len += ZLIB_SIZE_OFFSET;

    return b64_out_buff;
}

Bytef*
decode_binary(char* input_map, int start_position, int end_position, int compression_method, size_t* out_len)
{
    if(compression_method == _zlib_)
        return (Bytef*)zlib_fun(input_map, start_position, end_position, out_len);
    else if(compression_method == _no_comp_)
        return (Bytef*)no_comp_fun(input_map, start_position, end_position, out_len);
    else
    {
        exit(-1);
    }
}