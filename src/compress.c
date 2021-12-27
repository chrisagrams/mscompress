#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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


cmp_blk_vector_t*
alloc_cmp_buff(int allocation)
/**
 * @brief Allocates a cmp_blk_vector_t struct to store compressed blocks (cmp_block_t struct) in a vector.
 * 
 * @param allocation Initial size of vector.
 * 
 * @return An allocated and populated cmp_blk_vector_t struct.
 * 
 */
{
    cmp_blk_vector_t* r;

    r = (cmp_blk_vector_t*)malloc(sizeof(cmp_blk_vector_t));
    r->allocated = allocation;
    r->populated = 0;
    r->cmp_blks = (cmp_block_t**)malloc(sizeof(cmp_block_t*)*r->allocated);

    return r;
}


void
grow_cmp_buff(cmp_blk_vector_t** cmp_buff)
/**
 * @brief Grows a cmp_blk_vector_t struct. Deallocated old cmp_blk_vector_t and replaces in place with new pointer.
 * 
 * @param cmp_buff A dereferenced pointer to desired cmp_blk_vector_t to grow. New cmp_blk_vector_t will be located at the same addresss.
 * 
 */
{
    cmp_blk_vector_t* new_cmp_buff;
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
append_cmp_block(cmp_blk_vector_t** cmp_buff, cmp_block_t* buff)
/**
 * @brief Appends a compressed block to the compressed block vector. Grows vector if needed.
 * 
 * @param cmp_buff A dereferenced pointer to cmp_blk_vector_t vector.
 * 
 * @param buff Desired compressed block to append to vector.
 * 
 */
{
    if ((*cmp_buff)->populated == (*cmp_buff)->allocated) grow_cmp_buff(cmp_buff);

    (*cmp_buff)->cmp_blks[(*cmp_buff)->populated] = buff;
    (*cmp_buff)->populated++;

    return;
}

cmp_block_t*
pop_cmp_block(cmp_blk_vector_t** cmp_buff)
{
    cmp_block_t* r;
    int i = 0;

    if((*cmp_buff)->populated == 0) return NULL;

    r = (*cmp_buff)->cmp_blks[0];

    for(i; i < (*cmp_buff)->populated - 1; i++)
        (*cmp_buff)->cmp_blks[i] = (*cmp_buff)->cmp_blks[i+1];
    
    (*cmp_buff)->populated--;

    return r;

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


void
cmp_routine(ZSTD_CCtx* czstd,
                cmp_blk_vector_t** cmp_buff,
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

        printf("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", (*cmp_buff)->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

        *tot_size += (*curr_block)->size; *tot_cmp += cmp_len;

        append_cmp_block(cmp_buff, cmp_block);

        dealloc_data_block(*curr_block);

        *curr_block = alloc_data_block(cmp_blk_size);

        append_mem(*curr_block, input, len);
    }
}


void
cmp_flush(ZSTD_CCtx* czstd,
              cmp_blk_vector_t** cmp_buff,
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

    printf("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", (*cmp_buff)->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

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
cmp_dump(cmp_blk_vector_t** cmp_buff, int fd)
{
    cmp_block_t* front;

    while((*cmp_buff)->populated > 0)
    {
        front = pop_cmp_block(cmp_buff);
        printf("writing %ld bytes to disk\n", front->size);
        write_cmp_blk(front, fd);
        dealloc_cmp_block(front);

    }
}

void
cmp_xml_routine(ZSTD_CCtx* czstd,
                cmp_blk_vector_t** cmp_buff,
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
                   cmp_blk_vector_t** cmp_buff,
                   data_block_t** curr_block,
                   data_format* df,
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




cmp_blk_vector_t*
compress_xml(char* input_map, data_positions* dp, size_t cmp_blk_size, int fd)
/**
 * @brief Main function to compress XML data within a .mzML data.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @param dp A data_positions struct populated by preprocess.c:find_binary()
 * 
 * @param cmp_blk_size The size of a data block before it is compressed. 
 *                     NOTE: If this value is set too small, the program will exit with error code 1.
 *                           The current routine does not handle a substring that is too big for a 
 *                           data block. This should not be an issue with reasonable a reasonable
 *                           block size (>10 MB). This gives the user the flexbility to test 
 *                           performance with different block sizes.
 * 
 * @return A cmp_blk_vector_t struct representing a vector of compressed buffers to be written to disk.
 */
{
    ZSTD_CCtx* czstd;

    cmp_blk_vector_t* cmp_buff;
    data_block_t* curr_block;

    size_t len;

    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 1;

    czstd = alloc_cctx();

    cmp_buff = alloc_cmp_buff(10);                                    /* Start with 10 vectors, dynamically grow (x2) if run out of space */

    curr_block = alloc_data_block(cmp_blk_size);

    printf("\tUsing %ld byte block size.\n", cmp_blk_size);
    printf("\t========================== Data blocks ===========================\n");
    printf("\t||   Block index       Original size    Compressed size      %%  ||\n");
    printf("\t||==============================================================||\n");

    len = dp->start_positions[0];                                     /* Base case: Position 0 to first <binary> tag */

    cmp_xml_routine(czstd, &cmp_buff, &curr_block,
                    input_map,                                        /* Offset is 0 */
                    len, cmp_blk_size, &tot_size, &tot_cmp);


    for(i; i < dp->total_spec * 2; i++)
    {

        len = dp->start_positions[i]-dp->end_positions[i-1];          /* Length is substring between start[i] - end[i-1] */

        cmp_xml_routine(czstd, &cmp_buff, &curr_block,
                        input_map + dp->end_positions[i-1],           /* Offset is end of previous </binary> */
                        len, cmp_blk_size, &tot_size, &tot_cmp);
                        
        cmp_dump(&cmp_buff, fd);


    }

    len = dp->file_end - dp->end_positions[dp->total_spec*2];         /* End case: from last </binary> to EOF */

    cmp_xml_routine(czstd, &cmp_buff, &curr_block,
                    input_map + dp->end_positions[dp->total_spec*2],  /* Offset is end of last </binary> */
                    len, cmp_blk_size, &tot_size, &tot_cmp);


    cmp_flush(czstd, &cmp_buff, &curr_block, cmp_blk_size, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    cmp_dump(&cmp_buff, fd);

    printf("\t==================================================================\n");
    printf("\tXML size: %ld bytes. Compressed XML size: %ld bytes. (%1.2f%%)\n", tot_size, tot_cmp, (double)tot_size/tot_cmp);

    /* Cleanup (curr_block already freed by cmp_flush) */
    dealloc_cctx(czstd);

    return cmp_buff;
}

cmp_blk_vector_t*
compress_binary(char* input_map, data_positions* dp, data_format* df, size_t cmp_blk_size, int fd)
{
    ZSTD_CCtx* czstd;

    cmp_blk_vector_t* cmp_buff;
    data_block_t* curr_block;

    size_t len;

    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 0;

    czstd = alloc_cctx();

    cmp_buff = alloc_cmp_buff(10);

    curr_block = alloc_data_block(cmp_blk_size);

    printf("\t========================== Data blocks ===========================\n");
    printf("\t||   Block index       Original size    Compressed size      %%  ||\n");
    printf("\t||==============================================================||\n");

    for(i; i < dp->total_spec * 2; i++)
    {
        len = dp->end_positions[i] - dp->start_positions[i];

        cmp_binary_routine(czstd, &cmp_buff, &curr_block, df, 
                           input_map+dp->start_positions[i],
                           len, cmp_blk_size, &tot_size, &tot_cmp);
        cmp_dump(&cmp_buff, fd);

    }

    cmp_flush(czstd, &cmp_buff, &curr_block, cmp_blk_size, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    cmp_dump(&cmp_buff, fd);

    printf("\t==================================================================\n");

    printf("\tBinary size: %ld bytes. Compressed binary size: %ld bytes. (%1.2f%%)\n", tot_size, tot_cmp, (double)tot_size/tot_cmp);

    /* Cleanup (curr_block already freed by cmp_flush) */
    dealloc_cctx(czstd);

    return cmp_buff;
}