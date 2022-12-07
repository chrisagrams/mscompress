#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vendor/zlib/zlib.h"
#include <sys/time.h>
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
    r = malloc(sizeof(data_block_t));
    r->mem = malloc(sizeof(char)*max_size);
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
alloc_cmp_block(char* mem, size_t size, size_t original_size)
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
    r = malloc(sizeof(cmp_block_t));
    r->mem = mem;
    r->size = size;
    r->original_size = original_size;
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
/**
 * @brief Allocates and initializes a compress_args_t struct to be passed to compress_routine.
 * 
 * @param input_map Pointer representing position within mmap'ed mzML file.
 * 
 * @param dp  
 * 
 */

    compress_args_t* r;

    r = malloc(sizeof(compress_args_t));

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

        cmp_block = alloc_cmp_block(cmp, cmp_len, (*curr_block)->size);

        // print("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", cmp_buff->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

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

    cmp_block = alloc_cmp_block(cmp, cmp_len, (*curr_block)->size);

    // print("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", cmp_buff->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

    *tot_size += (*curr_block)->size; *tot_cmp += cmp_len;

    append_cmp_block(cmp_buff, cmp_block);

    dealloc_data_block(*curr_block);
}

void
write_cmp_blk(cmp_block_t* blk, int fd)
/**
 * @brief Writes cmp_block_t block to file. Program exits if writing to file fails.
 * 
 * @param blk A cmp_block_t with mem and size populated.
 * 
 * @param fd File descriptor to write to.
 */
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
cmp_dump(cmp_blk_queue_t* cmp_buff,
         block_len_queue_t* blk_len_queue,
         int fd)
/**
 * @brief Pops cmp_block_t from queue, appends block_len to block_len_queue, and writes cmp_block_t to file.
 *        Write to disk is timed to display write speed.
 * 
 * @param cmp_buff A cmp_blk_queue_t to pop from.
 * 
 * @param blk_len_queue A block_len_queue_t to append a block_len_t to.
 * 
 * @param fd File descriptor to write cmp_blk to.
 */
{
    cmp_block_t* front;
    clock_t start, stop;

    while(cmp_buff->populated > 0)
    {
        front = pop_cmp_block(cmp_buff);

        append_block_len(blk_len_queue, front->original_size, front->size);

        start = clock();
        write_cmp_blk(front, fd);
        stop = clock();

        print("\tWrote %ld bytes to disk (%1.2fmb/s)\n", front->size, ((double)front->size/1000000)/((double)(stop-start)/CLOCKS_PER_SEC));

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
/**
 * @brief cmp_routine wrapper for XML data.
 */
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
/**
 * @brief cmp_routine wrapper for binary data. 
 *        Decodes base64 binary with encoding specified within df->compression before compression.
 */
{
    size_t binary_len = 0;
    char* binary_buff; 
    
    binary_buff = df->decode_source_compression_fun(input, 0, len, &binary_len);

    cmp_routine(czstd,
                cmp_buff,
                curr_block,
                binary_buff,
                binary_len,
                cmp_blk_size,
                tot_size, tot_cmp);
                
    free(binary_buff);
}

void
compress_routine(void* args)
/**
 * @brief Compress routine. Iterates through data_positions and compresses XML and binary with a single pass.
 *        Returns a cmp_blk_queue containing a linked-list queue of compressed blocks. 
 *        Return value is stored within args->ret.
 * 
 * @param args Function arguments allocated and populated by alloc_compress_args
 */

{
    int tid;

    ZSTD_CCtx* czstd;

    compress_args_t* cb_args;
    cmp_blk_queue_t* cmp_buff;
    data_block_t* curr_block;

    size_t len = 0;

    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 0;

    tid = get_thread_id();

    cb_args = (compress_args_t*)args;

    czstd = alloc_cctx();

    cmp_buff = alloc_cmp_buff();

    curr_block = alloc_data_block(cb_args->cmp_blk_size);

    for(; i < cb_args->dp->total_spec; i++)
    {
        len = cb_args->dp->end_positions[i] - cb_args->dp->start_positions[i];
        
        if(cb_args->df)
            cmp_binary_routine(czstd, cmp_buff, &curr_block, cb_args->df, 
                        cb_args->input_map + cb_args->dp->start_positions[i],
                        len, cb_args->cmp_blk_size, &tot_size, &tot_cmp);
        else
            cmp_xml_routine(czstd, cmp_buff, &curr_block,
                        cb_args->input_map + cb_args->dp->start_positions[i],           
                        len, cb_args->cmp_blk_size, &tot_size, &tot_cmp);

    }

    cmp_flush(czstd, cmp_buff, &curr_block, cb_args->cmp_blk_size, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    print("\tThread %03d: Input size: %ld bytes. Compressed size: %ld bytes. (%1.2f%%)\n", tid, tot_size, tot_cmp, (double)tot_size/tot_cmp);

    /* Cleanup (curr_block already freed by cmp_flush) */
    dealloc_cctx(czstd);

    cb_args->ret = cmp_buff;
}

block_len_queue_t*
compress_parallel(char* input_map,
                  data_positions_t** ddp,
                  data_format_t* df,
                  size_t cmp_blk_size,
                  int divisions, int threads, int fd)
/**
 * @brief Creates and executes compression threads using compress_routine.
 * 
 * @param input_map Pointer representing position within mmap'ed mzML file.
 * 
 * @param ddp An array of data_positions_t struct with size equals to divisions.
 * 
 * @param df Data format struct.
 * 
 * @param cmp_blk_size The size of a data block before it is compressed. 
 * 
 * @param divisions Number of divisions within ddp.
 * 
 * @param threads Number of threads to create at a given time.
 * 
 * @param fd File descriptor to write to.
 */
{
    block_len_queue_t* blk_len_queue;
    compress_args_t* args[divisions];
    pthread_t ptid[divisions];
    
    int i;

    blk_len_queue = alloc_block_len_queue();

    int divisions_used = 0;

    while(divisions_used < divisions)
    {
        
        for(i = divisions_used; i < divisions_used + threads; i++)
            args[i] = alloc_compress_args(input_map, ddp[i], df, cmp_blk_size);

        for(i = divisions_used; i < divisions_used + threads; i++)
            pthread_create(&ptid[i], NULL, &compress_routine, (void*)args[i]);

        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            pthread_join(ptid[i], NULL);
            cmp_dump(args[i]->ret, blk_len_queue, fd);
            dealloc_compress_args(args[i]);
        }
        divisions_used += threads;
    }
    return blk_len_queue;
}

void 
compress_mzml(char* input_map,
              long blocksize,
              int divisions,
              int threads,
              footer_t* footer,
              data_positions_t** ddp,
              data_format_t* df,
              data_positions_t** mz_binary_divisions,
              data_positions_t** int_binary_divisions,
              data_positions_t** xml_divisions,
              int output_fd)
{

    block_len_queue_t *xml_block_lens, *mz_binary_block_lens, *int_binary_block_lens;

    struct timeval start, stop;

    gettimeofday(&start, NULL);

    print("\nDecoding and compression...\n");

    print("\t===XML===\n");
    footer->xml_pos = get_offset(output_fd);
    xml_block_lens = compress_parallel((char*)input_map, xml_divisions, NULL, blocksize, divisions, threads, output_fd);  /* Compress XML */

    print("\t===m/z binary===\n");
    footer->mz_binary_pos = get_offset(output_fd);
    mz_binary_block_lens = compress_parallel((char*)input_map, mz_binary_divisions, df, blocksize, divisions, threads, output_fd); /* Compress m/z binary */

    print("\t===int binary===\n");
    footer->int_binary_pos = get_offset(output_fd);
    int_binary_block_lens = compress_parallel((char*)input_map, int_binary_divisions, df, blocksize, divisions, threads, output_fd); /* Compress int binary */

    // Dump block_len_queue to msz file.
    footer->xml_blk_pos = get_offset(output_fd);
    dump_block_len_queue(xml_block_lens, output_fd);

    footer->mz_binary_blk_pos = get_offset(output_fd);
    dump_block_len_queue(mz_binary_block_lens, output_fd);

    footer->int_binary_blk_pos = get_offset(output_fd);
    dump_block_len_queue(int_binary_block_lens, output_fd);

    footer->xml_ddp_pos = get_offset(output_fd);
    dump_ddp(xml_divisions, divisions, output_fd);

    footer->mz_ddp_pos = get_offset(output_fd);
    dump_ddp(mz_binary_divisions, divisions, output_fd);

    footer->int_ddp_pos = get_offset(output_fd);
    dump_ddp(int_binary_divisions, divisions, output_fd);

    // footer->num_spectra = dp->total_spec;

    // footer->file_end = dp->file_end;

    footer->divisions = divisions;

    write_footer(footer, output_fd);

    gettimeofday(&stop, NULL);

    print("Decoding and compression time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));
}