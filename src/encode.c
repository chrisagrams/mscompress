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
#include "vendor/zlib/zlib.h"
#include <assert.h>


void
encode_base64(zlib_block_t* zblk, char* dest, size_t src_len, size_t* out_len)
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
    if(zblk == NULL || zblk->buff == NULL)
        error("encode_base64: zblk is NULL");

    if(dest == NULL)
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

void
encode_zlib_fun_no_header(char** src, size_t src_len, char* dest, size_t* out_len)
{
    // assert(0); // this is broken now, need to fix
    if(src == NULL || *src == NULL)
        error("encode_zlib_fun: src is NULL");

    if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
        error("encode_zlib_fun: src_len is invalid");

    if (dest == NULL)
        error("encode_zlib_fun: dest is NULL");

    if (out_len == NULL)
        error("encode_zlib_fun: out_len is NULL");

    Bytef* zlib_encoded;

    size_t zlib_len = 0;
    
    zlib_block_t* decmp_input;

    zlib_block_t* cmp_output;
 
    decmp_input = zlib_alloc(ZLIB_SIZE_OFFSET);
    decmp_input->mem = *src;
    decmp_input->buff = decmp_input->mem + decmp_input->offset;

    cmp_output = zlib_alloc(0);

    // void* decmp_header = zlib_pop_header(decmp_input);

    // uint16_t org_len = *(uint16_t*)decmp_header;

    zlib_len = (size_t)zlib_compress(((Bytef*)*src), cmp_output, src_len);
    // zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET, cmp_output, src_len);


    free(decmp_input);
    // free(decmp_header);

    encode_base64(cmp_output, dest, zlib_len, out_len);
    
    *src += (ZLIB_SIZE_OFFSET + src_len);
}

void
encode_zlib_fun_w_header(char** src, size_t src_len, char* dest, size_t* out_len)
{
    if(src == NULL || *src == NULL)
        error("encode_zlib_fun: src is NULL");

    if (src_len <= 0 || src_len > ZLIB_BUFF_FACTOR)
        error("encode_zlib_fun: src_len is invalid");

    if (dest == NULL)
        error("encode_zlib_fun: dest is NULL");

    if (out_len == NULL)
        error("encode_zlib_fun: out_len is NULL");

    Bytef* zlib_encoded;

    size_t zlib_len = 0;
    
    zlib_block_t* decmp_input;

    zlib_block_t* cmp_output;
 
    decmp_input = zlib_alloc(ZLIB_SIZE_OFFSET);
    decmp_input->mem = *src;
    decmp_input->buff = decmp_input->mem + decmp_input->offset;

    cmp_output = zlib_alloc(0);

    void* decmp_header = zlib_pop_header(decmp_input);

    uint16_t org_len = *(uint16_t*)decmp_header;

    zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET, cmp_output, org_len);
    // zlib_len = (size_t)zlib_compress(((Bytef*)*src) + ZLIB_SIZE_OFFSET, cmp_output, src_len);


    free(decmp_input);
    free(decmp_header);

    encode_base64(cmp_output, dest, zlib_len, out_len);
    
    *src += (ZLIB_SIZE_OFFSET + org_len);
}

void
encode_no_comp_fun(char** src, size_t src_len, char* dest, size_t* out_len)
{
    Bytef* zlib_encoded;

    size_t zlib_len = 0;
    
    zlib_block_t* decmp_input = malloc(sizeof(zlib_block_t));
    if(decmp_input == NULL)
        error("encode_no_comp_fun: malloc failed");
 
    decmp_input->mem = *src;
    decmp_input->offset = ZLIB_SIZE_OFFSET;
    decmp_input->buff = decmp_input->mem + ZLIB_SIZE_OFFSET;

    void* decmp_header = zlib_pop_header(decmp_input);

    uint16_t org_len = *(uint16_t*)decmp_header;

    encode_base64(decmp_input, dest, org_len, out_len);

    *src += (ZLIB_SIZE_OFFSET + org_len);
}

encode_fun_ptr
set_encode_fun(int compression_method, char* lossy)
{
    if(lossy == NULL)
        error("set_encode_fun: lossy is NULL");
    switch(compression_method)
    {
        case _zlib_:
            if(strcmp(lossy, "lossless") == 0 || *lossy == '0' || *lossy == "")
                return encode_zlib_fun_w_header;
            else
                return encode_zlib_fun_no_header;    
        case _no_comp_:
            return encode_no_comp_fun;
        default:
            error("Invalid compression method.");
            return NULL;
    }
}
