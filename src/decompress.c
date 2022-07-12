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
decmp_binary_block(void* decmp_binary, size_t blk_size)
{
    size_t binary_len; 
    char* binary_str;
    size_t consumed = 0;

    while(consumed < blk_size)
    {
        binary_str = encode_binary(((char**)&decmp_binary), &binary_len);
        consumed += binary_len;
    }
    // binary_str = encode_binary((&decmp_binary), &binary_len);
}

void*
decmp_routine(void* input_map,
              long xml_offset,
              long binary_offset,
              data_positions_t* dp,
              block_len_t* xml_blk,
              block_len_t* binary_blk,
              size_t* out_len)
{
    ZSTD_DCtx* dctx;
    void* decmp_xml;
    void* decmp_binary;

    dctx = alloc_dctx();

    decmp_xml = decmp_block(dctx, input_map, xml_offset, xml_blk->compressed_size, xml_blk->original_size);
    decmp_binary = decmp_block(dctx, input_map, binary_offset, binary_blk->compressed_size, binary_blk->original_size);

    size_t binary_len; 
    char* binary_str;

    int64_t buff_off = 0;
    int64_t xml_off = dp->start_positions[0];

    int64_t len = dp->file_end;
    int64_t curr_len = dp->end_positions[0]-dp->start_positions[0];


    char* buff = malloc(len);

    memcpy(buff, decmp_xml, curr_len);
    buff_off += curr_len;
    xml_off += curr_len;

    int i = 1;

    while(xml_off < xml_blk->original_size)
    {
        binary_str = encode_binary(((char**)&decmp_binary), &binary_len);
        if(binary_str == NULL)
        {
            //if encode_binary returns null, dump all buff contents to file for debug
            *out_len = buff_off;
            return buff;
        }
        memcpy(buff+buff_off, binary_str, binary_len);
        buff_off+=binary_len;
        curr_len = dp->end_positions[i]-dp->start_positions[i];
        if (curr_len < 0) //temp fix
        {
            *out_len = buff_off;
            return buff;
        }
        memcpy(buff+buff_off, decmp_xml+xml_off, curr_len);
        buff_off+=curr_len;
        xml_off += curr_len;
        i++;
    }

    *out_len = buff_off;

    return buff;

}