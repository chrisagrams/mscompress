#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ZSTD_CCtx*
alloc_cctx()
{
    ZSTD_CCtx* cctx;
    cctx = ZSTD_createCCtx();
    if(!cctx) 
    {
        perror("ZSTD Context failed.");
        exit(1);
    }
    return cctx;

}

void*
alloc_ztsd_buff(size_t src_len, size_t* buff_len)
{
    size_t bound;

    bound = ZSTD_compressBound(src_len);

    *buff_len = bound;

    return malloc(bound);
}


void *
zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level)
{
    void* out_buff;
    size_t buff_len = 0;


    out_buff = alloc_ztsd_buff(src_len, &buff_len);

    *out_len = ZSTD_compressCCtx(cctx, out_buff, buff_len, src_buff, src_len, compression_level);

    if (!*out_len)
        return NULL;

    return out_buff;
}