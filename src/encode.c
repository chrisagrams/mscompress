#include "mscompress.h"
#include "vendor/base64/include/libbase64.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>


char*
encode_base64(zlib_block_t* zblk, size_t src_len, size_t* out_len)
{
    char* b64_out_buff;

    b64_out_buff = (char*)malloc(sizeof(char)*src_len*2);

    base64_encode(zblk->buff, src_len, b64_out_buff, out_len, 0);

    free(zblk);

    return b64_out_buff;
}

char*
encode_binary(char** src, size_t* out_len)
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

    zlib_len = (size_t)zlib_compress((Bytef*)*src+ZLIB_SIZE_OFFSET, cmp_output, org_len);

    *src += (ZLIB_SIZE_OFFSET + org_len);

    free(decmp_input);
    free(decmp_header);

    return encode_base64(cmp_output, zlib_len, out_len);
}