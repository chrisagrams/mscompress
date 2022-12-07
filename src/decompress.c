#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

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
decmp_block(ZSTD_DCtx* dctx, void* input_map, long offset, block_len_t* blk)
{
    return zstd_decompress(dctx, input_map+offset, blk->compressed_size, blk->original_size);
}

decompress_args_t*
alloc_decompress_args(char* input_map,
                      data_format_t* df,
                      block_len_t* xml_blk,
                      block_len_t* mz_binary_blk,
                      block_len_t* int_binary_blk,
                      data_positions_t** mz_binary_divisions,
                      data_positions_t** int_binary_divisions,
                      data_positions_t** xml_divisions,
                      int division,
                      off_t footer_xml_off,
                      off_t footer_mz_bin_off,
                      off_t footer_int_bin_off)
{
    decompress_args_t* r;
    
    r = (decompress_args_t*)malloc(sizeof(decompress_args_t));

    r->input_map = input_map;
    r->df = df;
    r->xml_blk = xml_blk;
    r->mz_binary_blk = mz_binary_blk;
    r->int_binary_blk = int_binary_blk;
    r->mz_binary_divisions = mz_binary_divisions;
    r->int_binary_divisions = int_binary_divisions;
    r->xml_divisions = xml_divisions;
    r->division = division;
    r->footer_xml_off = footer_xml_off;
    r->footer_mz_bin_off = footer_mz_bin_off;
    r->footer_int_bin_off = footer_int_bin_off;

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

int
get_lowest(int i_0, int i_1, int i_2)
{
    int ret = -1;

    if(i_0 < i_1 && i_0 < i_2) 
        ret = 0;
    else if (i_1 < i_0 && i_1 < i_2)
        ret = 1;
    else if (i_2 < i_0 && i_2 < i_1)
        ret = 2;    

    return ret;
}


void
decompress_routine(void* args)
{
    // Get thread ID
    int tid = get_thread_id();

    // Allocate a decompression context
    ZSTD_DCtx* dctx = alloc_dctx();

    if(dctx == NULL)
    {
        fprintf(stderr, "Failed to allocate decompression context.\n");
        exit(-1);
    }

    decompress_args_t* db_args = (decompress_args_t*)args;

    if(db_args == NULL)
    {
        fprintf(stderr, "Decompression arguments are null.\n");
        exit(-1);
    }

    // Decompress each block of data
    void
        *decmp_xml = decmp_block(dctx, db_args->input_map, db_args->footer_xml_off, db_args->xml_blk),
        *decmp_mz_binary = decmp_block(dctx, db_args->input_map, db_args->footer_mz_bin_off, db_args->mz_binary_blk),
        *decmp_int_binary = decmp_block(dctx, db_args->input_map, db_args->footer_int_bin_off, db_args->int_binary_blk);

    size_t binary_len = 0;

    int64_t buff_off = 0,
            xml_off = 0, mz_off = 0, int_off = 0;
    int64_t xml_i = 0, mz_i = 0, int_i = 0;        

    // Determine starting block (xml, mz, int)
    off_t xml_start = db_args->xml_divisions[db_args->division]->start_positions[0],
          mz_start  = db_args->mz_binary_divisions[db_args->division]->start_positions[0],
          int_start = db_args->int_binary_divisions[db_args->division]->start_positions[0];

    int block = get_lowest(xml_start, mz_start, int_start); // xml = 0/2, mz = 1, int = 3, error = -1

    if(block == -1)
    {
        fprintf(stderr, "Error determining starting block.\n");
        exit(-1);
    }

    long len = db_args->xml_blk->original_size + db_args->mz_binary_blk->original_size + db_args->int_binary_blk->original_size;

    char* buff = malloc(len*2);
    db_args->ret = buff;

    int64_t curr_len = 0;

    algo_args* a_args = (algo_args*)malloc(sizeof(algo_args));
    size_t algo_output_len = 0;
    a_args->dest_len = &algo_output_len;
    a_args->enc_fun = db_args->df->encode_source_compression_fun;

    while(block != -1)
    {
        switch (block)
        {
        case 0: // xml
            if(xml_i == db_args->xml_divisions[db_args->division]->total_spec) {block = -1; break;}
            curr_len = db_args->xml_divisions[db_args->division]->end_positions[xml_i] - db_args->xml_divisions[db_args->division]->start_positions[xml_i];
            assert(curr_len > 0 && curr_len < len);
            memcpy(buff + buff_off, decmp_xml + xml_off, curr_len);
            xml_off += curr_len;
            buff_off += curr_len;
            xml_i++;
            block++;
            break;
        case 1: // mz
            if(mz_i == db_args->mz_binary_divisions[db_args->division]->total_spec) {block = -1; break;}
            curr_len = db_args->mz_binary_divisions[db_args->division]->end_positions[mz_i] - db_args->mz_binary_divisions[db_args->division]->start_positions[mz_i];
            assert(curr_len > 0 && curr_len < len);
            a_args->src = (char**)&decmp_mz_binary;
            a_args->src_len = curr_len;
            a_args->dest = buff+buff_off;
            db_args->df->target_mz_fun((void*)a_args);
            buff_off += curr_len;
            mz_i++;
            block++;
            break;
        case 2: // xml
            if(xml_i == db_args->xml_divisions[db_args->division]->total_spec) {block = -1; break;}
            curr_len = db_args->xml_divisions[db_args->division]->end_positions[xml_i] - db_args->xml_divisions[db_args->division]->start_positions[xml_i];
            assert(curr_len > 0 && curr_len < len);
            memcpy(buff + buff_off, decmp_xml + xml_off, curr_len);
            xml_off += curr_len;
            buff_off += curr_len;
            xml_i++;
            block++;
            break;
        case 3:
            if(int_i == db_args->int_binary_divisions[db_args->division]->total_spec) {block = -1; break;}
            curr_len = db_args->int_binary_divisions[db_args->division]->end_positions[int_i] - db_args->int_binary_divisions[db_args->division]->start_positions[int_i];
            assert(curr_len > 0 && curr_len < len);
            a_args->src = (char**)&decmp_int_binary;
            a_args->src_len = curr_len;
            a_args->dest = buff+buff_off;
            db_args->df->target_int_fun((void*)a_args);
            buff_off += curr_len;
            int_i++;
            block = 0;
            break;
        case -1:
            break;
        }
    }

    db_args->ret_len = buff_off;

    return;
}

void
decompress_parallel(char* input_map,
                    block_len_queue_t* xml_block_lens,
                    block_len_queue_t* mz_binary_block_lens,
                    block_len_queue_t* int_binary_block_lens,
                    data_positions_t** mz_binary_divisions,
                    data_positions_t** int_binary_divisions,
                    data_positions_t** xml_divisions,
                    data_format_t* df,
                    footer_t* msz_footer,
                    int divisions, int threads, int fd)
{
    decompress_args_t* args[divisions];
    pthread_t ptid[divisions];

    block_len_t *xml_blk, *mz_binary_blk, *int_binary_blk;

    off_t footer_xml_off = 0, footer_mz_bin_off = 0, footer_int_bin_off = 0; // offset within corresponding data_block.

    int i;

    int divisions_used = 0;

    clock_t start, stop;
    
    while(divisions_used < divisions)
    {
        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            xml_blk = pop_block_len(xml_block_lens);
            mz_binary_blk = pop_block_len(mz_binary_block_lens);
            int_binary_blk = pop_block_len(int_binary_block_lens);

            args[i] = alloc_decompress_args(input_map,
                                            df,
                                            xml_blk,
                                            mz_binary_blk,
                                            int_binary_blk,
                                            mz_binary_divisions,
                                            int_binary_divisions,
                                            xml_divisions,
                                            i,
                                            footer_xml_off + msz_footer->xml_pos,
                                            footer_mz_bin_off + msz_footer->mz_binary_pos,
                                            footer_int_bin_off + msz_footer->int_binary_pos);

            footer_xml_off += xml_blk->compressed_size;
            footer_mz_bin_off += mz_binary_blk->compressed_size;
            footer_int_bin_off += int_binary_blk->compressed_size;
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