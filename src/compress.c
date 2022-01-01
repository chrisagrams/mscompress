#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


ZSTD_CCtx*
alloc_cctx()
/**
 * @brief Creates a ZSTD compression context and handles errors.
 * 
 * @return A ZSTD compression context on success. Exits on error.
 * 
 */
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


void
dealloc_cctx(ZSTD_CCtx* cctx)
/**
 * @brief Deallocates a ZSTD compression context.
 * 
 * @param cctx A ZSTD compression context to free.
 * 
 */
{
    ZSTD_freeCCtx(cctx); /* Never fails. */
}


void*
alloc_ztsd_cbuff(size_t src_len, size_t* buff_len)
/**
 * @brief Allocates a compression buffer for ZSTD with size based on ZSTD_compressBound.
 * 
 * @param scr_len Length of string to compress.
 * 
 * @param buff_len A pass-by-reference return value of the size of the buffer.
 * 
 * @return A buffer of size buff_len
 * 
 */
{
    size_t bound;

    bound = ZSTD_compressBound(src_len);

    *buff_len = bound;

    return malloc(bound);
}


void *
zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level)
/**
 * @brief A wrapper function for ZSTD_compressCCtx.
 * This function allows the reuse of a ZSTD compression context per thread to reduce resource consumption.
 * This function takes care of allocating the proper buffer and handling errors.
 * 
 * @param cctx A ZSTD compression context allocated by alloc_cctx() (one per thread).
 * 
 * @param src_buff Source string to compress.
 * 
 * @param src_len Length of the source string.
 * 
 * @param out_len A pass-by-reference return value of the resulting compress string.
 * 
 * @param compresssion_level An integer (1-9) representing ZSTD compression strategy (see ZSTD documentation)
 *
 * @return A buffer with the compressed string on success, NULL on error.
 */
{
    void* out_buff;
    size_t buff_len = 0;


    out_buff = alloc_ztsd_cbuff(src_len, &buff_len);

    *out_len = ZSTD_compressCCtx(cctx, out_buff, buff_len, src_buff, src_len, compression_level);

    if (!*out_len) return NULL;

    return out_buff;
}


cmp_blk_queue_t*
alloc_cmp_buff()
/**
 * @brief Allocates a cmp_blk_vector_t struct to store compressed blocks (cmp_block_t struct) in a vector.
 * 
 * @param allocation Initial size of vector.
 * 
 * @return An allocated and populated cmp_blk_vector_t struct.
 * 
 */
{
    cmp_blk_queue_t* r;

    r = (cmp_blk_queue_t*)malloc(sizeof(cmp_blk_queue_t));
    r->populated = 0;
    r->head = NULL;
    r->tail = NULL;

    return r;
}

void
dealloc_cmp_buff(cmp_blk_queue_t* queue)
{
    if(queue)
    {
        if(queue->head)
        {
            cmp_block_t* curr_head = queue->head;
            cmp_block_t* new_head = curr_head->next;
            while(new_head)
            {
                free(curr_head);
                curr_head = new_head;
                new_head = curr_head->next;
            }
        }
        free(queue);
    }
}

void
append_cmp_block(cmp_blk_queue_t* cmp_buff, cmp_block_t* buff)
{
    cmp_block_t* old_tail;

    old_tail = cmp_buff->tail;
    if(old_tail)
    {
        old_tail->next = buff;
        cmp_buff->tail = buff;
    }
    else
    {
        cmp_buff->head = buff;
        cmp_buff->tail = buff;
    }
    cmp_buff->populated++;

    return;
}

cmp_block_t*
pop_cmp_block(cmp_blk_queue_t* cmp_buff)
{
    cmp_block_t* old_head;

    old_head = cmp_buff->head;
    
    if(cmp_buff->head == cmp_buff->tail)
    {
        cmp_buff->head = NULL;
        cmp_buff->tail = NULL;
    }
    else
        cmp_buff->head = old_head->next;

    old_head->next = NULL;

    cmp_buff->populated--;

    return old_head;

}

data_block_t*
alloc_data_block(size_t max_size)
/**
 * @brief Allocates a data_block_t struct.
 * 
 * @param max_size The maximum size the data block can store (cmp_blk_size)
 * 
 * @return A populated data_block_t struct with capacity max_size and max_size field set.
 */
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
/**
 * @brief Deallocates a data_block_struct and its members.
 * 
 * @param db An allocated data_block_struct.
 * 
 */
{
    if (db)
    {
        if (db->mem) free(db->mem);
        free(db);
    }
    return;
}


cmp_block_t*
alloc_cmp_block(char* mem, size_t size)
/**
 * @brief Allocates and populates a cmp_block_t struct to store a compressed block.
 * 
 * @param mem Contents of compressed block.
 * 
 * @param size Length of the compressed block.
 * 
 * @returns A populated cmp_block_t struct with contents of compressed block.
 * 
 */
{
    cmp_block_t* r;
    r = (cmp_block_t*)malloc(sizeof(cmp_block_t));
    r->mem = mem;
    r->size = size;
    return r;
}

void
dealloc_cmp_block(cmp_block_t* blk)
{
    if(blk)
    {
        if (blk->mem) free(blk->mem);
        free(blk);
    }
    return;
}


int
append_mem(data_block_t* data_block, char* mem, size_t buff_len)
/**
 * @brief Appends data to a data block.
 * 
 * @param data_block Data block struct to append to.
 * 
 * @param mem Desired contents to append.
 * 
 * @param buff_len Length of contents to append
 * 
 * @return 1 on success, 0 if there is not enough space in data_block to append.
 * 
 */
{
    if(buff_len+data_block->size > data_block->max_size) return 0;

    memcpy(data_block->mem+data_block->size, mem, buff_len);
    data_block->size += buff_len;

    return 1;
}

compress_args_t*
alloc_compress_args(char* input_map, data_positions_t* dp, data_format_t* df, size_t cmp_blk_size)
{
    compress_args_t* r;

    r = (compress_args_t*)malloc(sizeof(compress_args_t));

    r->input_map = input_map;
    r->dp = dp;
    r->df = df;
    r->cmp_blk_size = cmp_blk_size;

    r->ret = NULL;

    return r;
}

void
dealloc_compress_args(compress_args_t* args)
{
    if(args)
    {
        if(args->ret) dealloc_cmp_buff(args->ret);
        free(args);
    }
}


void
cmp_routine(ZSTD_CCtx* czstd,
                cmp_blk_queue_t* cmp_buff,
                data_block_t** curr_block,
                char* input,
                size_t len,
                size_t cmp_blk_size,
                size_t* tot_size,
                size_t* tot_cmp)
/**
 * @brief A routine to compress an XML block.
 * Given an offset within an .mzML document and length, this function will append the text data to 
 * a data block until it is full. Once a data block is full, the data block will be compressed using
 * zstd_compress() and a cmp_block will be allocated, populated, and appened to the cmp_buff. After 
 * compression, the old data block will be deallocated.
 * 
 * @param czstd A ZSTD compression context allocated by alloc_cctx() (one per thread).
 * 
 * @param cmp_buff A dereferenced pointer to the cmp_buff vector.
 * 
 * @param curr_block Current data block to append to and/or compress.
 * 
 * @param input A pointer within the .mzML document to store within a data block.
 * 
 * @param len The length of the desired substring to compress within the .mzML document.
 * 
 * @param cmp_blk_size The size of a data block before it is compressed. 
 *                     NOTE: If this value is set too small, the program will exit with error code 1.
 *                           The current routine does not handle a substring that is too big for a 
 *                           data block. This should not be an issue with reasonable a reasonable
 *                           block size (>10 MB). This gives the user the flexbility to test 
 *                           performance with different block sizes.
 * 
 * @param tot_size A pass-by-reference variable to bookkeep total number of XML bytes processed.
 * 
 * @param tot_cmp A pass-by-reference variable to bookkeep total compressed size of XML.
 * 
 */
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;

    if(len > cmp_blk_size) {
        fprintf(stderr, "err: cmp_blk_size too small (%ld > %ld)\n", len, cmp_blk_size);
        exit(1);
    }

    if(!append_mem(*curr_block, input, len))
    {
        cmp = zstd_compress(czstd, (*curr_block)->mem, (*curr_block)->size, &cmp_len, 1);

        cmp_block = alloc_cmp_block(cmp, cmp_len);

        // printf("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", cmp_buff->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

        *tot_size += (*curr_block)->size; *tot_cmp += cmp_len;

        append_cmp_block(cmp_buff, cmp_block);

        dealloc_data_block(*curr_block);

        *curr_block = alloc_data_block(cmp_blk_size);

        append_mem(*curr_block, input, len);
    }
}


void
cmp_flush(ZSTD_CCtx* czstd,
              cmp_blk_queue_t* cmp_buff,
              data_block_t** curr_block,
              size_t cmp_blk_size,
              size_t* tot_size,
              size_t* tot_cmp)
/**
 * @brief "Flushes" the current data block by compressing and appending to cmp_buff vector.
 * Handles the remainder of data blocks stored in the cmp_routine that did not fully populate
 * a data block to be compressed.
 * 
 * @param czstd A ZSTD compression context allocated by alloc_cctx() (one per thread).
 * 
 * @param cmp_buff A dereferenced pointer to the cmp_buff vector.
 * 
 * @param curr_block Current data block to append to and/or compress.
 * 
 * @param cmp_blk_size The size of a data block before it is compressed. 
 *                     NOTE: If this value is set too small, the program will exit with error code 1.
 *                           The current routine does not handle a substring that is too big for a 
 *                           data block. This should not be an issue with reasonable a reasonable
 *                           block size (>10 MB). This gives the user the flexbility to test 
 *                           performance with different block sizes.
 * 
 * @param tot_size A pass-by-reference variable to bookkeep total number of XML bytes processed.
 * 
 * @param tot_cmp A pass-by-reference variable to bookkeep total compressed size of XML.
 */
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;


    if((*curr_block)->size > cmp_blk_size) {
        fprintf(stderr, "err: cmp_blk_size too small (%ld > %ld)\n", (*curr_block)->size, cmp_blk_size);
        exit(1);
    }

    cmp = zstd_compress(czstd, (*curr_block)->mem, (*curr_block)->size, &cmp_len, 1);

    cmp_block = alloc_cmp_block(cmp, cmp_len);

    // printf("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", cmp_buff->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

    *tot_size += (*curr_block)->size; *tot_cmp += cmp_len;

    append_cmp_block(cmp_buff, cmp_block);

    dealloc_data_block(*curr_block);
}

void
write_cmp_blk(cmp_block_t* blk, int fd)
{
    int rv;

    rv = write_to_file(fd, blk->mem, blk->size);
    
    if(rv != blk->size)
    {
        fprintf(stderr, "Did not write all bytes to disk.\n");
        exit(1);
    }
}

void
cmp_dump(cmp_blk_queue_t* cmp_buff, int fd)
{
    cmp_block_t* front;
    clock_t start;
    clock_t stop;

    while(cmp_buff->populated > 0)
    {
        front = pop_cmp_block(cmp_buff);

        start = clock();
        write_cmp_blk(front, fd);
        stop = clock();

        printf("\tWrote %ld bytes to disk (%1.2fmb/s)\n", front->size, ((double)front->size/1000000)/((double)(stop-start)/CLOCKS_PER_SEC));

        dealloc_cmp_block(front);
    }
}

void
cmp_xml_routine(ZSTD_CCtx* czstd,
                cmp_blk_queue_t* cmp_buff,
                data_block_t** curr_block,
                char* input,
                size_t len,
                size_t cmp_blk_size,
                size_t* tot_size,
                size_t* tot_cmp)
{
    cmp_routine(czstd,
                cmp_buff,
                curr_block,
                input,
                len,
                cmp_blk_size,
                tot_size,
                tot_cmp);
}

void
cmp_binary_routine(ZSTD_CCtx* czstd,
                   cmp_blk_queue_t* cmp_buff,
                   data_block_t** curr_block,
                   data_format_t* df,
                   char* input,
                   size_t len,
                   size_t cmp_blk_size,
                   size_t* tot_size,
                   size_t* tot_cmp)
{
    size_t binary_len = 0;
    double* binary_buff; 
    
    binary_buff = decode_binary(input, 0, len, df->compression, &binary_len);
    cmp_routine(czstd,
                cmp_buff,
                curr_block,
                (char*)binary_buff,
                binary_len,
                cmp_blk_size,
                tot_size, tot_cmp);
}

void
compress_routine(void* args)
{
    int tid;

    ZSTD_CCtx* czstd;

    compress_args_t* cb_args;
    cmp_blk_queue_t* cmp_buff;
    data_block_t* curr_block;

    size_t len;

    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 0;

    tid = get_thread_id();

    cb_args = (compress_args_t*)args;

    czstd = alloc_cctx();

    cmp_buff = alloc_cmp_buff();

    curr_block = alloc_data_block(cb_args->cmp_blk_size);

    for(i; i < cb_args->dp->total_spec; i++)
    {
        len = cb_args->dp->end_positions[i] - cb_args->dp->start_positions[i];

        if(cb_args->df)
            cmp_binary_routine(czstd, cmp_buff, &curr_block, cb_args->df, 
                           cb_args->input_map+cb_args->dp->start_positions[i],
                           len, cb_args->cmp_blk_size, &tot_size, &tot_cmp);
        else
            cmp_xml_routine(czstd, cmp_buff, &curr_block,
                        cb_args->input_map + cb_args->dp->start_positions[i],           
                        len, cb_args->cmp_blk_size, &tot_size, &tot_cmp);

    }

    cmp_flush(czstd, cmp_buff, &curr_block, cb_args->cmp_blk_size, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    printf("\tThread %03d: Input size: %ld bytes. Compressed size: %ld bytes. (%1.2f%%)\n", tid, tot_size, tot_cmp, (double)tot_size/tot_cmp);

    /* Cleanup (curr_block already freed by cmp_flush) */
    dealloc_cctx(czstd);

    cb_args->ret = cmp_buff;
}

void
compress_parallel(char* input_map, data_positions_t** ddp, data_format_t* df, size_t cmp_blk_size, int divisions, int fd)
{
    cmp_blk_queue_t* compressed_binary;
    compress_args_t* args[divisions];
    pthread_t ptid[divisions];
    
    int i = 0;

    for(i = 0; i < divisions; i++)
        args[i] = alloc_compress_args(input_map, ddp[i], df, cmp_blk_size);

    
    for(i = 0; i < divisions; i++)
        pthread_create(&ptid[i], NULL, &compress_routine, (void*)args[i]);

    for(i = 0; i < divisions; i++)
    {
        pthread_join(ptid[i], NULL);
        cmp_dump(args[i]->ret, fd);
        dealloc_compress_args(args[i]);
    }
}