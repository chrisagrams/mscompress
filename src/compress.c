#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "vendor/zlib/zlib.h"
#include <zstd.h>
#include "mscompress.h"


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
    
    #ifdef ERROR_CHECK
        if(!cctx) 
    if(!cctx) 
        if(!cctx) 
        {
            perror("ZSTD Context failed.");
            exit(1);
        }
    #endif

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
    #ifdef ERROR_CHECK
        if(src_len <= 0)
            error("alloc_ztsd_cbuff: invalid src_len for compression buffer.\n");
        if(buff_len == NULL)
            error("alloc_ztsd_cbuff: buff_len is NULL.\n");
    #endif

    size_t bound;

    bound = ZSTD_compressBound(src_len);

    *buff_len = bound;

    void* r = malloc(bound);

    #ifdef ERROR_CHECK
        if(r == NULL)
            error("alloc_ztsd_cbuff: malloc failed.\n");
    #endif

    return r;
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
    #ifdef ERROR_CHECK
        if(cctx == NULL)
            error("zstd_compress: cctx is NULL.\n");
        if(src_buff == NULL)
            error("zstd_compress: src_buff is NULL.\n");
        if(src_len < 0)
            error("zstd_compress: invalid src_len for compression.\n");
        if(out_len == NULL)
            error("zstd_compress: out_len is NULL.\n");
        if(compression_level < 1 || compression_level > 9)
            error("zstd_compress: invalid compression_level.\n");
    #endif

    void* out_buff;
    size_t buff_len = 0;

    if(src_len == 0)
    {
        *out_len = 0;
        return NULL;
    }

    out_buff = alloc_ztsd_cbuff(src_len, &buff_len);

    *out_len = ZSTD_compressCCtx(cctx, out_buff, buff_len, src_buff, src_len, compression_level);

    #ifdef ERROR_CHECK
        if (!*out_len)
            error("zstd_compress: ZSTD_compressCCtx failed.\n");
    #endif

    return out_buff;
}

void*
no_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len, size_t* out_len, int compression_level)
/*
    Same function signature as zstd_compress, but does not compress.
*/
{
    *out_len = src_len;
    void* out_buff = malloc(src_len);
    memcpy(out_buff, src_buff, src_len);
    return out_buff;
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
    while(buff_len + data_block->size >= data_block->max_size)                  // Not enough space in data block
        realloc_data_block(data_block, data_block->max_size * REALLOC_FACTOR); // Grow data block by REALLOC_FACTOR     

    if(data_block->mem + data_block->size == NULL || mem == NULL)             // Check for NULL pointers
        error("append_mem: NULL pointer passed to append_mem.\n");

    if(memcpy(data_block->mem + data_block->size, mem, buff_len) == NULL)     // Copy memory
        error("append_mem: Failed to append memory.\n");

    data_block->size += buff_len;                                           // Update size of data block

    return 1;
}

compress_args_t*
alloc_compress_args(char* input_map, data_positions_t* dp, data_format_t* df, compression_fun comp_fun, size_t cmp_blk_size, long blocksize, int mode)
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

    #ifdef ERROR_CHECK
        if(r == NULL)
            error("alloc_compress_args: Failed to allocate compress_args_t.\n");
    #endif

    r->input_map = input_map;
    r->dp = dp;
    r->df = df;
    r->comp_fun = comp_fun;
    r->cmp_blk_size = cmp_blk_size;
    r->blocksize = blocksize;
    r->mode = mode;

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
cmp_routine(    
                compression_fun compression_fun,
                ZSTD_CCtx* czstd,
                cmp_blk_queue_t* cmp_buff,
                data_block_t** curr_block,
                char* input,
                size_t len,
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
 * @param tot_size A pass-by-reference variable to bookkeep total number of XML bytes processed.
 * 
 * @param tot_cmp A pass-by-reference variable to bookkeep total compressed size of XML.
 * 
 */
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;
    size_t prev_size = 0;

    data_block_t* tmp_block = *curr_block;


    if(!append_mem((*curr_block), input, len))
    {
        cmp = compression_fun(czstd, (*curr_block)->mem, (*curr_block)->size, &cmp_len, 1);

        cmp_block = alloc_cmp_block(cmp, cmp_len, (*curr_block)->size);

        // print("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n", cmp_buff->populated, (*curr_block)->size, cmp_len, (double)(*curr_block)->size/cmp_len);

        *tot_size += (*curr_block)->size; *tot_cmp += cmp_len;

        append_cmp_block(cmp_buff, cmp_block);

        prev_size = (*curr_block)->size;

        dealloc_data_block(*curr_block);

        *curr_block = alloc_data_block(prev_size);

        append_mem(*curr_block, input, len);
    }
}


void
cmp_flush(    
              compression_fun compression_fun,
              ZSTD_CCtx* czstd,
              cmp_blk_queue_t* cmp_buff,
              data_block_t** curr_block,
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
 * @param tot_size A pass-by-reference variable to bookkeep total number of XML bytes processed.
 * 
 * @param tot_cmp A pass-by-reference variable to bookkeep total compressed size of XML.
 */
{
    void* cmp;
    cmp_block_t* cmp_block;
    size_t cmp_len = 0;

    #ifdef ERROR_CHECK
        if(!(*curr_block))
            error("cmp_flush: curr_block is NULL. This should not happen.\n");
    #endif
    
    cmp = compression_fun(czstd, (*curr_block)->mem, (*curr_block)->size, &cmp_len, 1);

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
       error("write_cmp_blk: Did not write all bytes to disk.\n");
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

    if(cmp_buff == NULL) return; // Nothing to do.

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


typedef void (*cmp_routine_func)(ZSTD_CCtx*, algo_args*, cmp_blk_queue_t*, data_block_t**, data_format_t*, char*, size_t, size_t*, size_t*);
typedef cmp_routine_func (*cmp_routine_func_ptr)();


void
cmp_xml_routine(
                compression_fun compression_fun,
                ZSTD_CCtx* czstd,
                algo_args* a_args,
                cmp_blk_queue_t* cmp_buff,
                data_block_t** curr_block,
                data_format_t* df,
                char* input,
                size_t len,
                size_t* tot_size,
                size_t* tot_cmp)
/**
 * @brief cmp_routine wrapper for XML data.
 */
{
    cmp_routine(
                compression_fun,
                czstd,
                cmp_buff,
                curr_block,
                input,
                len,
                tot_size,
                tot_cmp);
}

void
cmp_binary_routine(
                   compression_fun compression_fun,
                   ZSTD_CCtx* czstd,
                   algo_args* a_args,
                   cmp_blk_queue_t* cmp_buff,
                   data_block_t** curr_block,
                   data_format_t* df,
                   char* input,
                   size_t len,
                   size_t* tot_size,
                   size_t* tot_cmp)
/**
 * @brief cmp_routine wrapper for binary data. 
 *        Decodes base64 binary with encoding specified within df->compression before compression.
 */
{
    size_t binary_len = 0;
    char* binary_buff = NULL; 
    
    // df->decode_source_compression_fun(input, len, &binary_buff, &binary_len);

    if(a_args == NULL)
        error("cmp_binary_routine: Failed to allocate algo_args.\n");

    a_args->src = &input;
    a_args->src_len = len;    
    a_args->dest = &binary_buff;
    a_args->dest_len = &binary_len;
    a_args->src_format = df->source_mz_fmt; //TODO: This is a hack. Need to fix.

    df->target_mz_fun((void*)a_args); //TODO: This is a hack. Need to fix.

    if(binary_buff == NULL)
        error("cmp_binary_routine: binary_buff is NULL\n");

    cmp_routine(
                compression_fun,
                czstd,
                cmp_buff,
                curr_block,
                binary_buff,
                binary_len,
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
    int tid = get_thread_id();

    ZSTD_CCtx* czstd = alloc_cctx();

    compress_args_t* cb_args = (compress_args_t*)args;
    algo_args* a_args = malloc(sizeof(algo_args));
    a_args->tmp = alloc_data_block(cb_args->blocksize); // Allocate a temporary data_block to intermediately store data.
    a_args->z = alloc_z_stream(); // Allocate a z_stream to intermediately store data.

    if(cb_args == NULL)
        error("compress_routine: Invalid compress_args_t\n");

    if(cb_args->dp->total_spec == 0) return; // No data to compress.

    cmp_blk_queue_t* cmp_buff = alloc_cmp_buff();
    data_block_t* curr_block = alloc_data_block(cb_args->blocksize); // Allocate a data_block to store data.


    size_t len = 0;
    size_t tot_size = 0;
    size_t tot_cmp = 0;

    int i = 0;

    cmp_routine_func_ptr cmp_fun = NULL;
    
    if(cb_args->df)
        cmp_fun = cmp_binary_routine;
    else
        cmp_fun = cmp_xml_routine;

    if(cb_args->mode == _mass_)
        a_args->dec_fun = cb_args->df->decode_source_compression_mz_fun;
    else if(cb_args->mode == _intensity_)
        a_args->dec_fun = cb_args->df->decode_source_compression_inten_fun;
    else if(cb_args->mode == _xml_)
        a_args->dec_fun = NULL;
    else
        error("compress_routine: Invalid mode. Mode: %d\n", cb_args->mode);
    
    for(; i < cb_args->dp->total_spec; i++)
    {
        
        if(cb_args->dp->end_positions[i] < cb_args->dp->start_positions[i])
            error("compress_routine: Invalid data position. Start: %ld End: %ld\n", cb_args->dp->start_positions[i], cb_args->dp->end_positions[i]);

        len = cb_args->dp->end_positions[i] - cb_args->dp->start_positions[i];

        if(len < 0)
            error("compress_routine: Invalid data position. Start: %ld End: %ld\n", cb_args->dp->start_positions[i], cb_args->dp->end_positions[i]);
        
        char* map = cb_args->input_map + cb_args->dp->start_positions[i];

        if(len == 0) continue; // Skip empty data blocks (e.g. empty spectra)

        cmp_fun(cb_args->comp_fun, czstd, a_args, cmp_buff, &curr_block, cb_args->df, 
                    map,
                    len, &tot_size, &tot_cmp);
    }

    cmp_flush(cb_args->comp_fun, czstd, cmp_buff, &curr_block, &tot_size, &tot_cmp); /* Flush remainder datablocks */

    print("\tThread %03d: Input size: %ld bytes. Compressed size: %ld bytes. (%1.2f%%)\n", tid, tot_size, tot_cmp, (double)tot_size/tot_cmp);

    /* Cleanup (curr_block already freed by cmp_flush) */
    dealloc_cctx(czstd);
    dealloc_data_block(a_args->tmp);
    dealloc_z_stream(a_args->z);

    cb_args->ret = cmp_buff;
}

block_len_queue_t*
compress_parallel(char* input_map,
                  data_positions_t** ddp,
                  data_format_t* df,
                  compression_fun comp_fun,
                  size_t cmp_blk_size, long blocksize, int mode,
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
    compress_args_t** args = malloc(sizeof(compress_args_t*) * divisions);
    pthread_t* ptid = malloc(sizeof(pthread_t) * divisions);
    
    int i = 0;

    blk_len_queue = alloc_block_len_queue();

    int divisions_used = 0;
    int divisions_left = divisions;

    for(i = divisions_used; i < divisions; i++)
        args[i] = alloc_compress_args(input_map, ddp[i], df, comp_fun, cmp_blk_size, blocksize, mode);

    while(divisions_left > 0)
    {
        if(divisions_left < threads)
            threads = divisions_left;

        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            int ret = pthread_create(&ptid[i], NULL, &compress_routine, (void*)args[i]);
            if(ret != 0)
            {
                perror("pthread_create");
                exit(-1);
            }   
        }

        for(i = divisions_used; i < divisions_used + threads; i++)
        {
            int ret = pthread_join(ptid[i], NULL);
            if(ret != 0)
            {
                perror("pthread_join");
                exit(-1);
            }
            cmp_dump(args[i]->ret, blk_len_queue, fd);
            dealloc_compress_args(args[i]);
        }
        divisions_used += threads;
        divisions_left -= threads;
    }
    return blk_len_queue;
}

void 
compress_mzml(char* input_map,
              long blocksize,
              int threads,
              footer_t* footer,
              data_format_t* df,
              divisions_t* divisions,
              int output_fd)
{
    block_len_queue_t *xml_block_lens, *mz_binary_block_lens, *inten_binary_block_lens;

    data_positions_t **xml_divisions = join_xml(divisions),
                     **mz_divisions  = join_mz(divisions),
                     **inten_divisions = join_inten(divisions); 

    struct timeval start, stop;

    gettimeofday(&start, NULL);

    print("\nDecoding and compression...\n");

    print("\t===XML===\n");
    footer->xml_pos = get_offset(output_fd);
    xml_block_lens = compress_parallel((char*)input_map, xml_divisions, NULL, df->xml_compression_fun, blocksize, blocksize/3, _xml_, divisions->n_divisions, threads, output_fd);  /* Compress XML */
    free(xml_divisions);

    print("\t===m/z binary===\n");
    footer->mz_binary_pos = get_offset(output_fd);
    df->target_mz_fun = set_compress_algo(footer->mz_fmt, df->source_mz_fmt); //TODO, rename target_mz_fun
    mz_binary_block_lens = compress_parallel((char*)input_map, mz_divisions, df, df->mz_compression_fun, blocksize, blocksize/3, _mass_, divisions->n_divisions, threads, output_fd); /* Compress m/z binary */
    free(mz_divisions);

    print("\t===int binary===\n");
    footer->inten_binary_pos = get_offset(output_fd);
    df->target_mz_fun = set_compress_algo(footer->inten_fmt, df->source_mz_fmt);//TODO, rename target_mz_fun
    inten_binary_block_lens = compress_parallel((char*)input_map, inten_divisions, df, df->inten_compression_fun, blocksize, blocksize/3, _intensity_, divisions->n_divisions, threads, output_fd); /* Compress int binary */
    free(inten_divisions);

    // Dump block_len_queue to msz file.
    footer->xml_blk_pos = get_offset(output_fd);
    dump_block_len_queue(xml_block_lens, output_fd);

    footer->mz_binary_blk_pos = get_offset(output_fd);
    dump_block_len_queue(mz_binary_block_lens, output_fd);

    footer->inten_binary_blk_pos = get_offset(output_fd);
    dump_block_len_queue(inten_binary_block_lens, output_fd);

    gettimeofday(&stop, NULL);

    print("Decoding and compression time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));
}

compression_fun
set_compress_fun(int accession)
{   
    switch(accession)
    {
        case _ZSTD_compression_ :       return zstd_compress;
        case _no_comp_ :                return no_compress;
        default :                       error("Compression type not supported.");
    }
}

int
get_compress_type(char* arg)
{
    if(arg == NULL)
        error("Compression type not specified.");
    if(strcmp(arg, "zstd") == 0 || strcmp(arg, "ZSTD") == 0)
        return _ZSTD_compression_;
    if(strcmp(arg, "nocomp") == 0 || strcmp(arg, "none") == 0)
        return _no_comp_;
}