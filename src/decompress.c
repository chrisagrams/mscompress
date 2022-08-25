#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

ZSTD_DCtx*
alloc_dctx()
/**
 * @brief Creates a ZSTD decompression context and handles errors.
 * 
 * @return A ZSTD decompression context on success. Exits on error.
 * 
 */
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

decompress_args_t*
alloc_decompress_args(char* input_map,
                      int binary_encoding,
                      data_positions_t* dp,
                      block_len_t* xml_blk,
                      block_len_t* binary_blk,
                      off_t footer_xml_offset,
                      off_t footer_binary_offset)
{
    decompress_args_t* r;
    
    r = (decompress_args_t*)malloc(sizeof(decompress_args_t));

    r->input_map = input_map;
    r->binary_encoding = binary_encoding;
    r->dp = dp;
    r->xml_blk = xml_blk;
    r->binary_blk = binary_blk;
    r->footer_xml_offset = footer_xml_offset;
    r->footer_binary_offset = footer_binary_offset;

    r->ret = NULL;
    r->ret_len = 0;

    return r;
}

void
dealloc_decompress_args(decompress_args_t* args)
{
    if(args)
    {
        if(args->ret) free(args->ret);
        free(args);
    }
}

void
decompress_routine(void* args)
{
    int tid;

    ZSTD_DCtx* dctx;

    decompress_args_t* db_args;

    void *decmp_xml, *decmp_binary;

    tid = get_thread_id();

    dctx = alloc_dctx();

    db_args = (decompress_args_t*)args;

    decmp_xml = decmp_block(dctx, db_args->input_map, db_args->footer_xml_offset, db_args->xml_blk->compressed_size, db_args->xml_blk->original_size);
    decmp_binary = decmp_block(dctx, db_args->input_map, db_args->footer_binary_offset, db_args->binary_blk->compressed_size, db_args->binary_blk->original_size);

    size_t binary_len; 
    char* binary_str;

    int64_t buff_off, xml_off = 0;

    data_positions_t* dp = db_args->dp;

    int64_t len = dp->file_end;
    int64_t curr_len = dp->end_positions[0] - dp->start_positions[0];

    char* buff = malloc(len);

    db_args->ret = buff;

    memcpy(buff, decmp_xml, curr_len);
    buff_off += curr_len;
    xml_off += curr_len;

    int i = 1;

    int64_t bound = db_args->xml_blk->original_size;

    while(xml_off < bound)
    {
        /* encode binary and copy over to buffer */
        binary_str = encode_binary(((char**)&decmp_binary), db_args->binary_encoding, &binary_len);
        memcpy(buff + buff_off, binary_str, binary_len);
        buff_off += binary_len;

        /* copy over xml to buffer */
        curr_len = dp->end_positions[i] - dp->start_positions[i];
        memcpy(buff + buff_off, decmp_xml + xml_off, curr_len);
        buff_off += curr_len;
        xml_off += curr_len;

        i++;
    }

    db_args->ret_len = buff_off;

    print("\tThread %03d: Decompressed size: %ld bytes.\n", tid, buff_off);
}

void
decompress_parallel(char* input_map,
                    int binary_encoding, 
                    block_len_queue_t* xml_blks,
                    block_len_queue_t* binary_blks,
                    data_positions_t** ddp,
                    footer_t* msz_footer,
                    int divisions, int threads, int fd)
{
    decompress_args_t* args[divisions];
    pthread_t ptid[divisions];

    block_len_t *xml_blk, *binary_blk;
    off_t footer_xml_offset = msz_footer->xml_pos;
    off_t footer_binary_offset = msz_footer->binary_pos;

    int i;

    int divisions_used = 0;

    clock_t start, stop;
    
    while(divisions_used < divisions)
    {
        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            xml_blk = pop_block_len(xml_blks);
            binary_blk = pop_block_len(binary_blks);

            args[i] = alloc_decompress_args(input_map, binary_encoding, ddp[i], xml_blk, binary_blk, footer_xml_offset, footer_binary_offset);

            footer_xml_offset += xml_blk->compressed_size;
            footer_binary_offset += binary_blk->compressed_size;
        }

        for(i = divisions_used; i < divisions_used + threads; i++)
            pthread_create(&ptid[i], NULL, &decompress_routine, (void*)args[i]);

        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            pthread_join(ptid[i], NULL);

            start = clock();
            write_to_file(fd, args[i]->ret, args[i]->ret_len);
            stop = clock();

            print("\tWrote %ld bytes to disk (%1.2fmb/s)\n", args[i]->ret_len, ((double)args[i]->ret_len/1000000)/((double)(stop-start)/CLOCKS_PER_SEC));

            dealloc_decompress_args(args[i]);
        }
        divisions_used += threads;
    }
}