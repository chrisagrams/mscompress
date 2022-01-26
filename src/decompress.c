#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ZSTD_DCtx*
alloc_dctx()
{
    ZSTD_DCtx* dctx;
    dctx = ZSTD_createDCtx();
    if(!dctx) 
    {
        perror("ZSTD Context failed.");
        exit(1);
    }
    return dctx;

}

void*
alloc_ztsd_dbuff(size_t buff_len)
{
    // size_t bound;

    // bound = ZSTD_decompressBound(src_len);

    // *buff_len = bound;

    return malloc(buff_len);
}

void *
zstd_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len, size_t org_len)
{
    void* out_buff;
    size_t decmp_len = 0;

    out_buff = alloc_ztsd_dbuff(org_len);

    decmp_len = ZSTD_decompressDCtx(dctx, out_buff, org_len, src_buff, src_len);

    if (decmp_len != org_len)
        return NULL;

    return out_buff;

}

void*
decmp_block(ZSTD_DCtx* dctx, void* input_map, long offset, size_t compressed_len, size_t decompressed_len)
{
    return zstd_decompress(dctx, input_map+offset, compressed_len, decompressed_len);
}

void*
decmp_routine(void* input_map, long xml_offset, long binary_offset, data_positions_t* dp, block_len_t* xml_blk, block_len_t* binary_blk)
{
    ZSTD_DCtx* dctx;
    void* decmp_xml;
    void* decmp_binary;

    dctx = alloc_dctx();

    decmp_xml = decmp_block(dctx, input_map, xml_offset, xml_blk->compressed_size, xml_blk->original_size);
    decmp_binary = decmp_block(dctx, input_map, binary_offset, binary_blk->compressed_size, binary_blk->original_size);

    return NULL;

}