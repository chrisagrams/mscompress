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
alloc_ztsd_cbuff(size_t src_len, size_t* buff_len)
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


    out_buff = alloc_ztsd_cbuff(src_len, &buff_len);

    *out_len = ZSTD_compressCCtx(cctx, out_buff, buff_len, src_buff, src_len, compression_level);

    if (!*out_len) return NULL;

    return out_buff;
}

cmp_buff_t*
alloc_cmp_buff(int allocation)
{
    cmp_buff_t* r;

    r = (cmp_buff_t*)malloc(sizeof(cmp_buff_t));
    r->allocated = allocation;
    r->populated = 0;
    r->cmp_blks = (cmp_block_t**)malloc(sizeof(cmp_block_t*)*r->allocated);

    return r;
}

void
grow_cmp_buff(cmp_buff_t** cmp_buff)
{
    cmp_buff_t* new_cmp_buff;
    int i = 0;

    new_cmp_buff = alloc_cmp_buff((*cmp_buff)->allocated*2);

    for (i; i < (*cmp_buff)->populated; i++)
        new_cmp_buff->cmp_blks[i] = (*cmp_buff)->cmp_blks[i];

    new_cmp_buff->populated = (*cmp_buff)->populated;
    
    free(*cmp_buff);

    *cmp_buff = new_cmp_buff;
    
    return;
}

void
append_cmp_block(cmp_buff_t** cmp_buff, cmp_block_t* buff)
{
    if ((*cmp_buff)->populated == (*cmp_buff)->allocated) grow_cmp_buff(cmp_buff);

    (*cmp_buff)->cmp_blks[(*cmp_buff)->populated] = buff;
    (*cmp_buff)->populated++;

    return;
}

data_block_t*
alloc_data_block(size_t max_size)
{
    data_block_t* r;
    r = (data_block_t*)malloc(sizeof(data_block_t));
    r->mem = (char*)malloc(sizeof(char)*max_size);
    r->size = 0;
    r->max_size = max_size;
    return r;
}

void
dealloc_data_block(data_block_t* db)
{
    free(db->mem);
    free(db);

    return;
}

cmp_block_t*
alloc_cmp_block(char* mem, size_t size)
{
    cmp_block_t* r;
    r = (cmp_block_t*)malloc(sizeof(cmp_block_t));
    r->mem = mem;
    r->size = size;
    return r;
}

int
append_mem(data_block_t* data_buff, char* mem, size_t buff_len)
{
    if(buff_len+data_buff->size > data_buff->max_size) return 0;

    memcpy(data_buff->mem+data_buff->size, mem, buff_len);
    data_buff->size += buff_len;

    return 1;
}

void
cmp_xml_routine(ZSTD_CCtx* czstd,
                cmp_buff_t** cmp_buff,
                data_block_t* curr_block,
                char* input,
                size_t len,
                size_t cmp_blk_size,
                size_t* tot_size,
                size_t* tot_cmp)
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;

    if(len > cmp_blk_size) {
        fprintf(stderr, "err: cmp_blk_size too small (%ld > %ld)\n", len, cmp_blk_size);
        exit(1);
    }

    if(!append_mem(curr_block, input, len))
    {
        cmp = zstd_compress(czstd, curr_block->mem, curr_block->size, &cmp_len, 1);

        cmp_block = alloc_cmp_block(cmp, cmp_len);

        printf("\t[Block %d] Original size: %ld Compressed size: %ld\n", (*cmp_buff)->populated, curr_block->size, cmp_len);

        *tot_size += curr_block->size; *tot_cmp += cmp_len;

        append_cmp_block(cmp_buff, cmp_block);

        dealloc_data_block(curr_block);

        curr_block = alloc_data_block(cmp_blk_size);

        append_mem(curr_block, input, len);
    }
}

void
cmp_xml_flush(ZSTD_CCtx* czstd,
              cmp_buff_t** cmp_buff,
              data_block_t* curr_block,
              size_t cmp_blk_size,
              size_t* tot_size,
              size_t* tot_cmp)
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;


    if(curr_block->size > cmp_blk_size) {
        fprintf(stderr, "err: cmp_blk_size too small (%ld > %ld)\n", curr_block->size, cmp_blk_size);
        exit(1);
    }

    cmp = zstd_compress(czstd, curr_block->mem, curr_block->size, &cmp_len, 1);

    cmp_block = alloc_cmp_block(cmp, cmp_len);

    printf("\t[Block %d] Original size: %ld Compressed size: %ld\n", (*cmp_buff)->populated, curr_block->size, cmp_len);

    *tot_size += curr_block->size; *tot_cmp += cmp_len;

    append_cmp_block(cmp_buff, cmp_block);

    dealloc_data_block(curr_block);


}

cmp_buff_t*
compress_xml(char* input_map, data_positions* dp, size_t cmp_blk_size)
{
    ZSTD_CCtx* czstd;

    cmp_buff_t* cmp_buff;
    data_block_t* curr_block;

    size_t len;

    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 1;

    czstd = alloc_cctx();

    cmp_buff = alloc_cmp_buff(10);                                    /* Start with 10 vectors, dynamically grow (x2) if run out of space */

    curr_block = alloc_data_block(cmp_blk_size);

    printf("\tUsing %ld byte block size.\n", cmp_blk_size);


    len = dp->start_positions[0];                                     /* Base case: Position 0 to first <binary> tag */

    cmp_xml_routine(czstd, &cmp_buff, curr_block,
                    input_map,                                        /* Offset is 0 */
                    len, cmp_blk_size, &tot_size, &tot_cmp);


    for(i; i < dp->total_spec * 2; i++)
    {

        len = dp->start_positions[i]-dp->end_positions[i-1];          /* Length is substring between start[i] - end[i-1] */

        cmp_xml_routine(czstd, &cmp_buff, curr_block,
                        input_map + dp->end_positions[i-1],           /* Offset is end of previous </binary> */
                        len, cmp_blk_size, &tot_size, &tot_cmp);

    }

    len = dp->file_end - dp->end_positions[dp->total_spec*2];         /* End case: from last </binary> to EOF */

    cmp_xml_routine(czstd, &cmp_buff, curr_block,
                    input_map + dp->end_positions[dp->total_spec*2],  /* Offset is end of last </binary> */
                    len, cmp_blk_size, &tot_size, &tot_cmp);


    cmp_xml_flush(czstd, &cmp_buff, curr_block, cmp_blk_size, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    printf("\tXML size: %ld Compressed XML size: %ld (%1.2f%%)\n", tot_size, tot_cmp, (double)tot_size/tot_cmp);

    return cmp_buff;
}