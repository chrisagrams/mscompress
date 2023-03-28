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
#include <zlib.h>
#include "vendor/zlib/zlib.h"


char*
base64_alloc(size_t size)
{
    char* r;

    r = malloc(sizeof(char) * size);
    
    if(r == NULL)
        error("base64_alloc: failed to allocate memory.\n");
    
    return r;
}

void
decode_base64(char* src, char* dest, size_t src_len, size_t* out_len)
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
    int b64_ret = base64_decode(src, src_len, dest, out_len, 0);

    if(b64_ret == 0) 
        error("decode_base64: base64_decode returned with an error. (%d)\n", b64_ret);
}

void
decode_zlib_fun(z_stream* z, char* src, size_t src_len, char** dest, size_t* out_len, data_block_t* tmp)
/**
 * @brief Decodes an mzML binary block with "zlib" encoding.
 *        Decodes base64 string, zlib decodes the string, and appends resulting binary
 *        buffer with the length of the buffer stored within the first ZLIB_SIZE_OFFSET
 *        bytes of the buffer.
 *        Decoded binary data starts at b64_out_buff + ZLIB_SIZE_OFFSET.
 * 
 * @param input_map Pointer representing mmap'ed mzML file.
 * 
 * @param start_position Position of first byte of <binary> block.
 * 
 * @param end_position Position of last byte of </binary> block.
 * 
 * @param out_len Contains resulting buffer size on return.
 * 
 * @param tmp Pointer to data_block_t struct used for temporary storage.
 * 
 * @return A malloc'ed buffer with first ZLIB_SIZE_OFFSET bytes containing length of decoded binary
 *         and resulting decoded binary buffer.
 */
{
    if(src == NULL)
        error("decode_zlib_fun: src is NULL.\n");

    if(src_len <= 0)
        error("decode_zlib_fun: src_len is <= 0.\n");

    if(dest == NULL)
        error("decode_zlib_fun: dest is NULL.\n");

    if(out_len == NULL)
        error("decode_zlib_fun: out_len is NULL.\n");

    if(tmp == NULL)
        error("decode_zlib_fun: tmp is NULL.\n");

    if(z == NULL)
        error("decode_zlib_fun: z is NULL.\n");

    size_t b64_out_len = 0;

    // char* b64_out_buff = base64_alloc(sizeof(char) * src_len);

    if(tmp->max_size < src_len)
        realloc_data_block(tmp, src_len);
    
    char* b64_out_buff = tmp->mem;
    
    decode_base64(src, b64_out_buff, src_len, &b64_out_len);
    
    if (b64_out_buff == NULL)
        error("decode_zlib_fun: base64_decode returned with an error.\n");

    zlib_block_t* decmp_output = zlib_alloc(ZLIB_SIZE_OFFSET);

    uint16_t decmp_size = (uint16_t)zlib_decompress(z, b64_out_buff, decmp_output, b64_out_len);

    zlib_append_header(decmp_output, &decmp_size, ZLIB_SIZE_OFFSET);

    // free(b64_out_buff);
    
    *out_len = decmp_size + ZLIB_SIZE_OFFSET;
    
    *dest = (char*)decmp_output->mem;

    free(decmp_output);
}

void
decode_zlib_fun_no_header(z_stream* z, char* src, size_t src_len, char** dest, size_t* out_len, data_block_t* tmp)
{
    if(src == NULL)
        error("decode_zlib_fun_no_header: src is NULL.\n");

    if(src_len <= 0)
        error("decode_zlib_fun_no_header: src_len is <= 0.\n");

    if(dest == NULL)
        error("decode_zlib_fun_no_header: dest is NULL.\n");

    if(out_len == NULL)
        error("decode_zlib_fun_no_header: out_len is NULL.\n");

    if(z == NULL)
        error("decode_zlib_fun: z is NULL.\n");

    size_t b64_out_len = 0;

    if(tmp->max_size < src_len)
        realloc_data_block(tmp, src_len);
    
    char* b64_out_buff = tmp->mem;
    
    decode_base64(src, b64_out_buff, src_len, &b64_out_len);
    
    if (b64_out_buff == NULL)
        error("decode_zlib_fun_no_header: base64_decode returned with an error.\n");

    zlib_block_t* decmp_output = zlib_alloc(ZLIB_SIZE_OFFSET);

    uint16_t decmp_size = (uint16_t)zlib_decompress(z, b64_out_buff, decmp_output, b64_out_len);

    // free(b64_out_buff);
    
    *out_len = decmp_size;
    
    *dest = (char*)decmp_output->mem;

    free(decmp_output);
}

void
decode_no_comp_fun(z_stream* z, char* src, size_t src_len, char** dest, size_t* out_len, data_block_t* tmp)
/**
 * @brief Decodes an mzML binary block with "no comp" encoding.
 *        Decodes base64 string and appends a binary buffer with the length of the 
 *        buffer stored within the first ZLIB_SIZE_OFFSET bytes of the buffer.
 *        Decoded binary data starts at b64_out_buff + ZLIB_SIZE_OFFSET.
 * 
 * @param input_map Pointer representing mmap'ed mzML file.
 * 
 * @param start_position Position of first byte of <binary> block.
 * 
 * @param end_position Position of last byte of </binary> block.
 * 
 * @param out_len Contains resulting buffer size on return.
 * 
 * @return A malloc'ed buffer with first ZLIB_SIZE_OFFSET bytes containing length of decoded binary
 *         and resulting decoded binary buffer.
 */
{

    char* b64_out_buff;

    size_t header;

    b64_out_buff = base64_alloc(sizeof(char) * src_len + ZLIB_SIZE_OFFSET);

    decode_base64(src, b64_out_buff + ZLIB_SIZE_OFFSET, src_len, out_len);

    header = (uint16_t)(*out_len);

    memcpy(b64_out_buff, &header, ZLIB_SIZE_OFFSET);

    *out_len += ZLIB_SIZE_OFFSET;
    
    *dest = b64_out_buff;
}

decode_fun_ptr
set_decode_fun(int compression_method, int algo)
/**
 * @brief Returns appropriate decode function based on mzML file binary compression method.
 * 
 * @param compression_method Integer representing accession value of binary compression method.
 * 
 * @return A decode_fun_ptr representing a function pointer to appropriate decoding function.
 *         Exits program on failure.
 */
{
    if(algo == 0)
        error("set_decode_fun: lossy is 0.\n");
    switch (compression_method)
    {
    case _zlib_:
        if(algo == _lossless_)
            return decode_zlib_fun;
        else
            return decode_zlib_fun_no_header;
    case _no_comp_:
        return decode_no_comp_fun;
    default:
        error("set_decode_fun: Unknown source compression method.\n");
        return NULL; 
    }
}
