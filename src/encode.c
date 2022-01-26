#include "mscompress.h"
#include "vendor/base64/include/libbase64.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>


char*
encode_base64(char* src, size_t src_len, size_t* out_len)
{
    char* b64_out_buff;

    b64_out_buff = (char*)malloc(sizeof(char)*src_len*(4/3));

    base64_encode(src, src_len, b64_out_buff, out_len, 0);

    return b64_out_buff;
}

Bytef*
encode_zlib(Bytef* src, size_t* out_len, size_t src_len)
{
    int zlib_ret = 1;

    // size_t src_len = 0;

    Bytef* out_buff;

    // memcpy(&src_len, src, sizeof(uint16_t));

    out_buff = (Bytef*)malloc(sizeof(Bytef)*src_len*4);

    size_t test = 0;

    zlib_ret = compress(out_buff, &test, (Bytef*)(src), (uLong)src_len);

    switch (zlib_ret)
    {
    case Z_OK:
        return out_buff;
    case Z_MEM_ERROR:
        fprintf(stderr, "Z_MEM_ERROR\n");
        exit(1);
    case Z_BUF_ERROR:
        fprintf(stderr, "Z_BUFF_ERROR\n");
        exit(1);
    default:
        break;
    }
}

char*
encode_binary(char* src, size_t* out_len)
{
    Bytef* zlib_encoded;

    size_t zlib_len = 0;

    // zlib_encoded = encode_zlib((Bytef*)src, &zlib_len);

    return encode_base64(zlib_encoded, zlib_len, out_len);
}