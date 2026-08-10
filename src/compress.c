#include <assert.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/lz4/lib/lz4.h"
#include "../vendor/zlib/zlib.h"
#include "../vendor/zstd/lib/zstd.h"
#include "mscompress.h"

/*
 * Portable mutex/condition-variable/thread wrappers for the compression worker
 * pool below. Mirrors the mutex wrappers in queue.c; kept file-local so the
 * platform headers stay out of mscompress.h.
 */
#ifdef _WIN32
typedef CRITICAL_SECTION ms_mutex_t;
typedef CONDITION_VARIABLE ms_cond_t;
typedef HANDLE ms_thread_t;
#define MS_MUTEX_INIT(m) InitializeCriticalSection(m)
#define MS_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#define MS_MUTEX_LOCK(m) EnterCriticalSection(m)
#define MS_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define MS_COND_INIT(c) InitializeConditionVariable(c)
#define MS_COND_DESTROY(c) ((void)0)
#define MS_COND_WAIT(c, m) SleepConditionVariableCS((c), (m), INFINITE)
#define MS_COND_SIGNAL(c) WakeConditionVariable(c)
#define MS_COND_BROADCAST(c) WakeAllConditionVariable(c)
#else
typedef pthread_mutex_t ms_mutex_t;
typedef pthread_cond_t ms_cond_t;
typedef pthread_t ms_thread_t;
#define MS_MUTEX_INIT(m) pthread_mutex_init((m), NULL)
#define MS_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define MS_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define MS_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define MS_COND_INIT(c) pthread_cond_init((c), NULL)
#define MS_COND_DESTROY(c) pthread_cond_destroy(c)
#define MS_COND_WAIT(c, m) pthread_cond_wait((c), (m))
#define MS_COND_SIGNAL(c) pthread_cond_signal(c)
#define MS_COND_BROADCAST(c) pthread_cond_broadcast(c)
#endif

/**
 * @brief Creates a ZSTD compression context and handles errors.
 * @return A ZSTD compression context on success. `NULL` on error.
 */
ZSTD_CCtx* alloc_cctx() {
   ZSTD_CCtx* cctx;
   cctx = ZSTD_createCCtx();
   if (cctx == NULL)
      error("alloc_cctx: ZSTD Context failed.\n");
   return cctx;
}

/**
 * @brief Deallocates a ZSTD compression context.
 * @param cctx A pointer to the `ZSTD_CCtx` to be deallocated.
 */
void dealloc_cctx(ZSTD_CCtx* cctx) {
   ZSTD_freeCCtx(cctx); /* Never fails. */
}

/**
 * @brief Allocates a compression buffer for ZSTD with size based on
 * `ZSTD_compressBound`.
 * @param src_len Length of string to compress.
 * @param buff_len A pass-by-reference return value of the size of the buffer.
 * @return A buffer of size `buff_len` on success, `NULL` on error.
 */
void* alloc_zstd_cbuff(size_t src_len, size_t* buff_len) {
   if (src_len == 0) {
      *buff_len = 0;
      return NULL;
   }

   if (buff_len == NULL) {
      error("alloc_zstd_cbuff: buff_len is NULL.\n");
      return NULL;
   }

   size_t bound;

   bound = ZSTD_compressBound(src_len);

   *buff_len = bound;

   void* r = malloc(bound);

   if (r == NULL) {
      error("alloc_zstd_cbuff: malloc() error.\n");
      return NULL;
   }

   return r;
}

/**
 * @brief A wrapper function for `ZSTD_compressCCtx`.
 * This function allows the reuse of a ZSTD compression context per thread to
 * reduce resource consumption. This function takes care of allocating the
 * proper buffer and handling errors.
 *
 * @param cctx A ZSTD compression context allocated by `alloc_cctx()` (one per
 * thread).
 *
 * @param src_buff Source string to compress.
 *
 * @param src_len Length of the source string.
 *
 * @param out_len A pass-by-reference return value of the resulting compress
 * string.
 *
 * @param compression_level An integer (1-9) representing ZSTD compression
 * strategy (see ZSTD documentation)
 *
 * @return A buffer with the compressed string on success, `NULL` on error.
 */
void* zstd_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len,
                    size_t* out_len, int compression_level) {
   if (cctx == NULL) {
      error("zstd_compress: cctx is NULL.\n");
      return NULL;
   }
   if (src_buff == NULL) {
      error("zstd_compress: src_buff is NULL.\n");
      return NULL;
   }
   if (src_len < 0) {
      error("zstd_compress: invalid src_len for compression.\n");
      return NULL;
   }
   if (out_len == NULL) {
      error("zstd_compress: out_len is NULL.\n");
      return NULL;
   }
   if (compression_level < 1 || compression_level > 22) {
      error("zstd_compress: invalid compression_level.\n");
      return NULL;
   }

   void* out_buff;
   size_t buff_len = 0;

   if (src_len == 0) {
      *out_len = 0;
      return NULL;
   }

   out_buff = alloc_zstd_cbuff(src_len, &buff_len);

   if (out_buff == NULL) {
      error("zstd_compress: alloc_zstd_cbuff failed.\n");
      return NULL;
   }

   *out_len = ZSTD_compressCCtx(cctx, out_buff, buff_len, src_buff, src_len,
                                compression_level);

   if (!*out_len) {
      error("zstd_compress: ZSTD_compressCCtx failed.\n");
      free(out_buff);
      return NULL;
   }

   return out_buff;
}

/**
 * @brief A wrapper function for `LZ4_compress_default`.
 * This function allows the reuse of an LZ4 compression context per thread to
 * reduce resource consumption. This function takes care of allocating the
 * proper buffer and handling errors.
 * @param cctx A ZSTD compression context (not used in this function, but
 * included for consistency with other compression functions).
 * @param src_buff Source string to compress.
 * @param src_len Length of the source string.
 * @param out_len A pass-by-reference return value of the resulting compress
 * string.
 * @param compression_level An integer (1-12) representing LZ4 compression
 * strategy (see LZ4 documentation)
 * @return A buffer with the compressed string on success, `NULL` on error.
 */
void* lz4_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len,
                   size_t* out_len, int compression_level) {
   void* out_buff;
   int max_compressed_size;
   int compressed_data_size;

   if (src_buff == NULL) {
      warning("lz4_compress: src_buff is null.\n");
      return NULL;
   }
   if (src_len < 0) {
      warning("lz4_compress: src_len < 0.\n");
      return NULL;
   }
   if (out_len == NULL) {
      warning("lz4_compress: out_len is null.\n");
      return NULL;
   }
   if (compression_level < 1 || compression_level > 12) {
      warning("lz4_compress: compression_level out of bounds.\n");
      return NULL;
   }

   if (src_len == 0) {
      *out_len = 0;
      return NULL;
   }

   max_compressed_size = LZ4_compressBound(src_len);
   out_buff = malloc(max_compressed_size);

   if (out_buff == NULL) {
      warning("lz4_compress: error in malloc().\n");
      return NULL;
   }

   compressed_data_size =
       LZ4_compress_default(src_buff, out_buff, src_len, max_compressed_size);
   if (compressed_data_size > 0) {
      *out_len = compressed_data_size;
      return out_buff;
   } else {
      warning("lz4_compress: error in LZ4_compress_default\n");
      free(out_buff);
      return NULL;
   }

   return out_buff;
}

/**
 * @brief A no-op compression function that simply copies the input buffer to
 * the output buffer. Returns the output buffer on success, `NULL` on error.
 * @param cctx A ZSTD compression context (not used in this function, but
 * included for consistency with other compression functions).
 * @param src_buff The input buffer to be "compressed".
 * @param src_len The length of the input buffer.
 * @param out_len A pointer to a `size_t` where the size of the "compressed"
 * data will be stored.
 * @param compression_level The compression level to use (not used in this
 * function, but included for consistency with other compression functions).
 * @return A pointer to the "compressed" buffer on success. `NULL` on error.
 */
void* no_compress(ZSTD_CCtx* cctx, void* src_buff, size_t src_len,
                  size_t* out_len, int compression_level) {
   *out_len = src_len;
   void* out_buff = malloc(src_len);
   if (out_buff == NULL) {
      warning("no_compress: error in malloc()\n");
      return NULL;
   }
   memcpy(out_buff, src_buff, src_len);
   return out_buff;
}

/**
 * @brief Appends data to a data block, reallocating if necessary.
 * @param data_block Data block struct to append to.
 * @param mem Desired contents to append.
 * @param buff_len Length of contents to append.
 * @return 1 on success. Calls `error()` and aborts on `NULL` pointer or `memcpy` failure.
 * @note The data block is grown by `REALLOC_FACTOR` if there is not enough space.
 */
int append_mem(data_block_t* data_block, char* mem, size_t buff_len)
{
   while (buff_len + data_block->size >=
          data_block->max_size)  // Not enough space in data block
      realloc_data_block(
          data_block, data_block->max_size *
                          REALLOC_FACTOR);  // Grow data block by REALLOC_FACTOR

   if (data_block->mem + data_block->size == NULL ||
       mem == NULL)  // Check for NULL pointers
      error("append_mem: NULL pointer passed to append_mem.\n");

   if (memcpy(data_block->mem + data_block->size, mem, buff_len) ==
       NULL)  // Copy memory
      error("append_mem: Failed to append memory.\n");

   data_block->size += buff_len;  // Update size of data block

   return 1;
}

/**
 * @brief Allocates a `compress_args_t` struct.
 * @param input_map The input buffer containing the compressed data.
 * @param dp A pointer to a `data_positions_t` struct containing the data
 * positions.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param comp_fun A pointer to a compression function.
 * @param cmp_blk_size The size of the compression block.
 * @param blocksize The size of the block.
 * @param mode The mode of compression.
 * @return A pointer to the allocated `compress_args_t` struct on success. `NULL`
 * on error.
 */
compress_args_t* alloc_compress_args(char* input_map, data_positions_t* dp,
                                     data_format_t* df,
                                     compression_fun comp_fun,
                                     size_t cmp_blk_size, long blocksize,
                                     int mode) {
   compress_args_t* r;

   r = malloc(sizeof(compress_args_t));

   if (r == NULL) {
      error("alloc_compress_args: malloc() error.\n");
      return NULL;
   }

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

/**
 * @brief Deallocates a `compress_args_t` struct and its associated compressed buffer.
 * @param args A pointer to the `compress_args_t` struct to be deallocated.
 * @note If `args->ret` is non-`NULL`, the compressed buffer queue is freed via `dealloc_cmp_buff`
 *       before freeing the struct itself.
 */
void dealloc_compress_args(compress_args_t* args) {
   if (args) {
      if (args->ret)
         dealloc_cmp_buff(args->ret);
      free(args);
   }
}

/**
 * @brief Appends data to a data block and compresses when full.
 *
 * Given an offset within an .mzML document and length, this function will
 * append the text data to a data block until it is full. Once a data block is
 * full, the data block will be compressed and a `cmp_block` will be allocated,
 * populated, and appended to the `cmp_buff`. After compression, the old data
 * block will be deallocated and a new one allocated.
 *
 * @param compression_fun A function pointer to the compression function to use.
 * @param czstd A ZSTD compression context allocated by `alloc_cctx()` (one per thread).
 * @param compression_level An integer representing the ZSTD compression level.
 * @param cmp_buff A pointer to the compressed block queue to append results to.
 * @param curr_block Pointer to the current data block; replaced with a new block after compression.
 * @param input A pointer within the .mzML document to the data to compress.
 * @param len The length of the data to compress.
 * @param tot_size A pass-by-reference accumulator for total uncompressed bytes processed.
 * @param tot_cmp A pass-by-reference accumulator for total compressed bytes produced.
 */
void cmp_routine(compression_fun compression_fun, ZSTD_CCtx* czstd,
                 int compression_level, cmp_blk_queue_t* cmp_buff,
                 data_block_t** curr_block, char* input, size_t len,
                 size_t* tot_size, size_t* tot_cmp)
{
   void* cmp;
   cmp_block_t* cmp_block;
   size_t cmp_len = 0;
   size_t prev_size = 0;

   data_block_t* tmp_block = *curr_block;

   if (!append_mem((*curr_block), input, len)) {
      cmp = compression_fun(czstd, (*curr_block)->mem, (*curr_block)->size,
                            &cmp_len, compression_level);

      cmp_block = alloc_cmp_block(cmp, cmp_len, (*curr_block)->size);

      // print("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n",
      // cmp_buff->populated, (*curr_block)->size, cmp_len,
      // (double)(*curr_block)->size/cmp_len);

      *tot_size += (*curr_block)->size;
      *tot_cmp += cmp_len;

      append_cmp_block(cmp_buff, cmp_block);

      prev_size = (*curr_block)->size;

      dealloc_data_block(*curr_block);

      *curr_block = alloc_data_block(prev_size);

      append_mem(*curr_block, input, len);
   }
}

/**
 * @brief Flushes the current data block by compressing and appending to
 * `cmp_buff` vector. Handles the remainder of data blocks stored in the
 * `cmp_routine` that did not fully populate a data block to be compressed.
 *
 * @param compression_fun A function pointer to the compression function to be
 * used.
 * @param czstd A ZSTD compression context allocated by `alloc_cctx()` (one per
 * thread).
 * @param compression_level An integer representing the compression level.
 * @param cmp_buff A dereferenced pointer to the `cmp_buff` vector.
 * @param curr_block Current data block to append to and/or compress.
 * @param tot_size A pass-by-reference variable to bookkeep total number of XML
 * bytes processed.
 * @param tot_cmp A pass-by-reference variable to bookkeep total compressed size
 * of XML.
 * @return 0 on success, -1 on error.
 */
int cmp_flush(compression_fun compression_fun, ZSTD_CCtx* czstd,
              int compression_level, cmp_blk_queue_t* cmp_buff,
              data_block_t** curr_block, size_t* tot_size, size_t* tot_cmp) {
   void* cmp;
   cmp_block_t* cmp_block;
   size_t cmp_len = 0;

   if (!(*curr_block)) {
      error("cmp_flush: curr_block is NULL. This should not happen.\n");
      return -1;
   }

   cmp = compression_fun(czstd, (*curr_block)->mem, (*curr_block)->size,
                         &cmp_len, compression_level);

   cmp_block = alloc_cmp_block(cmp, cmp_len, (*curr_block)->size);
   if (cmp_block == NULL) {
      error("cmp_flush: Failed to allocate cmp_block.\n");
      return -1;
   }

   // print("\t||  [Block %05d]       %011ld       %011ld   %05.02f%%  ||\n",
   // cmp_buff->populated, (*curr_block)->size, cmp_len,
   // (double)(*curr_block)->size/cmp_len);

   *tot_size += (*curr_block)->size;
   *tot_cmp += cmp_len;

   append_cmp_block(cmp_buff, cmp_block);

   dealloc_data_block(*curr_block);
}

/**
 * @brief Writes a compressed block to file.
 *
 * @param blk A `cmp_block_t` with `mem` and `size` populated.
 * @param fd File descriptor to write to.
 * @return 0 on success, -1 on write failure.
 */
int write_cmp_blk(cmp_block_t* blk, int fd)
{
   size_t rv;

   rv = write_to_file(fd, blk->mem, blk->size);

   if (rv != blk->size) {
      error("write_cmp_blk: Did not write all bytes to disk.\n");
      return -1;
   }

   return 0;
}

/**
 * @brief Drains the compressed block queue, writing each block to file.
 *
 * Pops each `cmp_block_t` from the queue, records its block length metadata in
 * `blk_len_queue`, writes the compressed data to file, and deallocates the block.
 * Write speed is timed and printed.
 *
 * @param cmp_buff A `cmp_blk_queue_t` to pop compressed blocks from.
 * @param blk_len_queue A `block_len_queue_t` to append block length metadata to.
 * @param fd File descriptor to write compressed data to.
 *
 * @return Total number of bytes written to the file descriptor.
 */
size_t cmp_dump(cmp_blk_queue_t* cmp_buff, block_len_queue_t* blk_len_queue,
               int fd)
{
   cmp_block_t* front;
   double start, end;
   size_t total_written = 0;

   if (cmp_buff == NULL)
      return 0;  // Nothing to do.

   while (cmp_buff->populated > 0) {
      front = pop_cmp_block(cmp_buff);

      append_block_len(blk_len_queue, front->original_size, front->size);

      start = get_time();
      if (write_cmp_blk(front, fd)) {
         dealloc_cmp_block(front);
         return -1;
      }
      end = get_time();

      print("\tWrote %ld bytes to disk (%1.2fmb/s)\n", front->size,
            ((double)front->size / 1000000) / (end - start));

      total_written += front->size;
      dealloc_cmp_block(front);
   }

   return total_written;
}

typedef void (*cmp_routine_func)(compression_fun compression_fun, ZSTD_CCtx*,
                                 algo_args*, cmp_blk_queue_t*, data_block_t**,
                                 data_format_t*, char*, size_t, size_t*,
                                 size_t*);
typedef cmp_routine_func (*cmp_routine_func_ptr)();

/**
 * @brief Compression routine wrapper for XML data.
 *
 * Delegates to `cmp_routine` using the compression level from the data format.
 *
 * @param compression_fun A function pointer to the compression function to use.
 * @param czstd A ZSTD compression context allocated by `alloc_cctx()`.
 * @param a_args Algorithm arguments (unused for XML, included for interface consistency).
 * @param cmp_buff A pointer to the compressed block queue.
 * @param curr_block Pointer to the current data block being filled.
 * @param df A pointer to the `data_format_t` struct containing compression settings.
 * @param input A pointer to the XML data to compress.
 * @param len The length of the XML data.
 * @param tot_size A pass-by-reference accumulator for total uncompressed bytes.
 * @param tot_cmp A pass-by-reference accumulator for total compressed bytes.
 */
void cmp_xml_routine(compression_fun compression_fun, ZSTD_CCtx* czstd,
                     algo_args* a_args, cmp_blk_queue_t* cmp_buff,
                     data_block_t** curr_block, data_format_t* df, char* input,
                     size_t len, size_t* tot_size, size_t* tot_cmp)
{
   cmp_routine(compression_fun, czstd, df->zstd_compression_level, cmp_buff,
               curr_block, input, len, tot_size, tot_cmp);
}

/**
 * @brief Compression routine wrapper for binary (m/z or intensity) data.
 *
 * Decodes the source binary encoding (e.g., base64 + zlib) using the algorithm
 * function pointer in `df->target_mz_fun`, then delegates to `cmp_routine` for
 * block-based compression.
 *
 * @param compression_fun A function pointer to the compression function to use.
 * @param czstd A ZSTD compression context allocated by `alloc_cctx()`.
 * @param a_args Algorithm arguments struct used for decoding the source binary format.
 * @param cmp_buff A pointer to the compressed block queue.
 * @param curr_block Pointer to the current data block being filled.
 * @param df A pointer to the `data_format_t` struct containing format and compression settings.
 * @param input A pointer to the encoded binary data to decode and compress.
 * @param len The length of the encoded binary data.
 * @param tot_size A pass-by-reference accumulator for total uncompressed bytes.
 * @param tot_cmp A pass-by-reference accumulator for total compressed bytes.
 * @note The decoded binary buffer is freed after compression.
 */
void cmp_binary_routine(compression_fun compression_fun, ZSTD_CCtx* czstd,
                        algo_args* a_args, cmp_blk_queue_t* cmp_buff,
                        data_block_t** curr_block, data_format_t* df,
                        char* input, size_t len, size_t* tot_size,
                        size_t* tot_cmp)
{
   size_t binary_len = 0;
   char* binary_buff = NULL;

   // df->decode_source_compression_fun(input, len, &binary_buff, &binary_len);

   if (a_args == NULL)
      error("cmp_binary_routine: Failed to allocate algo_args.\n");

   a_args->src = &input;
   a_args->src_len = len;
   a_args->dest = &binary_buff;
   a_args->dest_len = &binary_len;

   if (a_args->algo_fun == NULL) {
      error("cmp_binary_routine: algo_fun is NULL, cannot compress binary block.\n");
      return;
   }

   a_args->algo_fun((void*)a_args);

   if (binary_buff == NULL)
      error("cmp_binary_routine: binary_buff is NULL\n");

   cmp_routine(compression_fun, czstd, df->zstd_compression_level, cmp_buff,
               curr_block, binary_buff, binary_len, tot_size, tot_cmp);

   free(binary_buff);
}

#ifdef _WIN32
/**
 * @brief Windows thread entry point for the compression routine.
 * @param lpParam A pointer to a `compress_args_t` struct containing compression arguments.
 * @return 0 on completion.
 */
DWORD WINAPI compress_routine_win(LPVOID lpParam) {
   compress_args_t* args = (compress_args_t*)lpParam;
   compress_routine(args);
   return 0;
}
#endif

/**
 * @brief Thread entry point for the compression pipeline.
 *
 * Iterates through `data_positions` and compresses XML or binary data in a single
 * pass. Selects `cmp_xml_routine` or `cmp_binary_routine` based on the mode field
 * in the arguments. Produces a `cmp_blk_queue` containing a linked-list of
 * compressed blocks stored in `args->ret`.
 *
 * @param args A void pointer to a `compress_args_t` struct allocated by `alloc_compress_args()`.
 * @return Always returns `NULL`. The compressed output is stored in `((compress_args_t*)args)->ret`.
 * @note Allocates and frees its own ZSTD compression context, `algo_args`, and temporary buffers.
 *       The caller is responsible for freeing `args->ret` via `dealloc_cmp_buff()`.
 * @warning This function is a pthread/Windows thread entry point. Do not call directly
 *          unless single-threaded execution is intended.
 */
void* compress_routine(void* args)
{
   int tid = get_thread_id();

   ZSTD_CCtx* czstd = alloc_cctx();

   compress_args_t* cb_args = (compress_args_t*)args;
   algo_args* a_args = malloc(sizeof(algo_args));
   if (a_args == NULL) {
      error("compress_routine: Failed to allocate algo_args.\n");
      return NULL;
   }

   a_args->tmp =
       alloc_data_block(cb_args->blocksize);  // Allocate a temporary data_block
                                              // to intermediately store data.

   if (a_args->tmp == NULL) {
      error("compress_routine: Failed to allocate data_block.\n");
      free(a_args);
      return NULL;
   }

   a_args->z =
       alloc_z_stream();  // Allocate a z_stream to intermediately store data.

   if (a_args->z == NULL) {
      error("compress_routine: Failed to allocate z_stream.\n");
      dealloc_data_block(a_args->tmp);
      free(a_args);
      return NULL;
   }

   a_args->z_inflate =
       alloc_z_stream_inflate();  // Dedicated inflate stream for decoding
                                  // source zlib data (init once, reset per
                                  // block).

   if (a_args->z_inflate == NULL) {
      error("compress_routine: Failed to allocate inflate z_stream.\n");
      dealloc_z_stream(a_args->z);
      dealloc_data_block(a_args->tmp);
      free(a_args);
      return NULL;
   }

   a_args->ret_code = 0;  // Initialize return code to 0 (success).

   if (cb_args == NULL)
      error("compress_routine: Invalid compress_args_t\n");

   if (cb_args->dp->total_spec == 0) {
      dealloc_cctx(czstd);
      dealloc_data_block(a_args->tmp);
      dealloc_z_stream(a_args->z);
      dealloc_z_stream_inflate(a_args->z_inflate);
      free(a_args);
      return NULL;  // No data to compress.
   }

   cmp_blk_queue_t* cmp_buff = alloc_cmp_buff();
   data_block_t* curr_block = alloc_data_block(
       cb_args->blocksize);  // Allocate a data_block to store data.

   size_t len = 0;
   size_t tot_size = 0;
   size_t tot_cmp = 0;

   int i = 0;

   cmp_routine_func cmp_fun = NULL;

   if (cb_args->mode == _xml_)
      cmp_fun = cmp_xml_routine;
   else
      cmp_fun = cmp_binary_routine;

   if (cb_args->mode == _mass_) {
      a_args->dec_fun = cb_args->df->decode_source_compression_mz_fun;
      a_args->scale_factor = cb_args->df->mz_scale_factor;
      a_args->src_format = cb_args->df->source_mz_fmt;
      a_args->algo_fun = cb_args->df->target_mz_fun;
   } else if (cb_args->mode == _intensity_) {
      a_args->dec_fun = cb_args->df->decode_source_compression_inten_fun;
      a_args->scale_factor = cb_args->df->int_scale_factor;
      a_args->src_format = cb_args->df->source_inten_fmt;
      a_args->algo_fun = cb_args->df->target_inten_fun;
   } else if (cb_args->mode == _xml_)
      a_args->dec_fun = NULL;
   else
      error("compress_routine: Invalid mode. Mode: %d\n", cb_args->mode);

   for (; i < cb_args->dp->total_spec; i++) {
      if (cb_args->dp->end_positions[i] < cb_args->dp->start_positions[i])
         error("compress_routine: Invalid data position. Start: %ld End: %ld\n",
               cb_args->dp->start_positions[i], cb_args->dp->end_positions[i]);

      len = cb_args->dp->end_positions[i] - cb_args->dp->start_positions[i];

      if (len < 0)
         error("compress_routine: Invalid data position. Start: %ld End: %ld\n",
               cb_args->dp->start_positions[i], cb_args->dp->end_positions[i]);

      char* map = cb_args->input_map + cb_args->dp->start_positions[i];

      if (len == 0)
         continue;  // Skip empty data blocks (e.g. empty spectra)

      cmp_fun(cb_args->comp_fun, czstd, a_args, cmp_buff, &curr_block,
              cb_args->df, map, len, &tot_size, &tot_cmp);
   }

   cmp_flush(cb_args->comp_fun, czstd, cb_args->df->zstd_compression_level,
             cmp_buff, &curr_block, &tot_size,
             &tot_cmp); /* Flush remainder datablocks */

   print(
       "\tThread %03d: Input size: %ld bytes. Compressed size: %ld bytes. "
       "(%1.2f%%)\n",
       tid, tot_size, tot_cmp, (double)tot_size / tot_cmp);

   /* Cleanup (curr_block already freed by cmp_flush) */
   dealloc_cctx(czstd);
   dealloc_data_block(a_args->tmp);
   dealloc_z_stream(a_args->z);
   dealloc_z_stream_inflate(a_args->z_inflate);
   free(a_args);

   cb_args->ret = cmp_buff;

   return NULL;
}

/**
 * @brief A persistent pool of compression workers feeding an ordered writer.
 *
 * The pool is allocated once per `compress_mzml()` call and reused across the
 * XML, m/z and intensity passes, so worker threads are no longer created and
 * destroyed per wave (or per pass). Within a pass, workers claim divisions
 * from `next_task` and compress them independently, while the thread that
 * called `compress_parallel()` acts as the single writer: it drains finished
 * divisions in strict order (0, 1, 2, ...) and appends them to the output file
 * descriptor as soon as each becomes available.
 *
 * Compression of later divisions therefore overlaps the writes of earlier
 * ones, and since exactly one thread ever writes — always in division order —
 * the resulting .msz byte layout is identical to the previous fork-join
 * implementation.
 *
 * `max_inflight` caps how far the workers may run ahead of the writer, which
 * bounds the amount of compressed-but-unwritten data held in memory.
 */
typedef struct {
   ms_mutex_t lock;
   ms_cond_t work; /* Workers wait here for a claimable unit of work. */
   ms_cond_t done; /* The writer waits here for its next division. */

   ms_thread_t* tids;
   int n_workers;
   int max_inflight;
   int shutdown;

   /* Per-pass state, all guarded by `lock`. */
   compress_args_t** args;
   char* done_flags;
   int n_divisions;
   int next_task;  /* Next division a worker may claim. */
   int next_write; /* Next division the writer expects. */

   /* Speculative pre-compression, active only while the preprocess scan is
      running. Guarded by `lock`. See spec_start(). */
   spec_ctx_t* spec;
} cmp_pool_t;

/* Speculative pre-compression helpers, defined further below. All of the
   `_locked` variants must be called with the pool lock held. */
static int spec_ready_locked(spec_ctx_t* spec);
static int spec_claim_locked(spec_ctx_t* spec);
static void spec_complete_locked(spec_ctx_t* spec);
static void spec_run_task(spec_ctx_t* spec, int idx);

/**
 * @brief Worker loop: claim work from the pool and compress it.
 *
 * Prefers speculative tasks while the preprocess scan is still running, then
 * falls back to divisions of the active ordered pass. Blocks while neither is
 * claimable — because no pass is active, because the scan has not yet reached
 * the next speculative task, or because the workers have run `max_inflight`
 * divisions ahead of the writer. Exits when the pool is shut down by
 * `dealloc_cmp_pool()`.
 *
 * @param pool The pool to pull work from.
 */
static void cmp_pool_work(cmp_pool_t* pool) {
   compress_args_t* args;
   int i;

   MS_MUTEX_LOCK(&pool->lock);

   for (;;) {
      while (!pool->shutdown && !spec_ready_locked(pool->spec) &&
             (pool->next_task >= pool->n_divisions ||
              pool->next_task - pool->next_write >= pool->max_inflight))
         MS_COND_WAIT(&pool->work, &pool->lock);

      if (pool->shutdown)
         break;

      if (spec_ready_locked(pool->spec)) {
         /* Hold the context across the unlock: spec_scan_finished() cannot
            clear pool->spec until every claimed task has completed. */
         spec_ctx_t* spec = pool->spec;

         i = spec_claim_locked(spec);
         MS_MUTEX_UNLOCK(&pool->lock);

         spec_run_task(spec, i);

         MS_MUTEX_LOCK(&pool->lock);
         spec_complete_locked(spec);
         MS_COND_SIGNAL(&pool->done);
         continue;
      }

      i = pool->next_task++;

      if (pool->done_flags[i]) /* Already satisfied by speculation. */
         continue;

      args = pool->args[i];

      MS_MUTEX_UNLOCK(&pool->lock);

      compress_routine((void*)args);

      MS_MUTEX_LOCK(&pool->lock);
      pool->done_flags[i] = 1;
      MS_COND_SIGNAL(&pool->done);
   }

   MS_MUTEX_UNLOCK(&pool->lock);
}

#ifdef _WIN32
/**
 * @brief Windows thread entry point for a compression pool worker.
 * @param lpParam A pointer to the `cmp_pool_t` to pull work from.
 * @return 0 on completion.
 */
static DWORD WINAPI cmp_pool_worker_win(LPVOID lpParam) {
   cmp_pool_work((cmp_pool_t*)lpParam);
   return 0;
}
#else
/**
 * @brief pthread entry point for a compression pool worker.
 * @param arg A pointer to the `cmp_pool_t` to pull work from.
 * @return Always `NULL`.
 */
static void* cmp_pool_worker(void* arg) {
   cmp_pool_work((cmp_pool_t*)arg);
   return NULL;
}
#endif

/**
 * @brief Allocates a compression worker pool and starts its threads.
 *
 * @param n_workers The number of worker threads to start.
 * @return A running `cmp_pool_t` on success, or `NULL` if fewer than two
 *         workers were requested (the caller then uses the serial path) or on
 *         allocation failure.
 * @note The caller is responsible for freeing the pool via `dealloc_cmp_pool()`.
 */
static cmp_pool_t* alloc_cmp_pool(int n_workers) {
   cmp_pool_t* pool;
   int i;

   if (n_workers < 2)
      return NULL; /* Serial fallback, see compress_parallel(). */

   pool = calloc(1, sizeof(cmp_pool_t));
   if (pool == NULL) {
      error("alloc_cmp_pool: calloc() error.\n");
      return NULL;
   }

   pool->tids = malloc(sizeof(ms_thread_t) * n_workers);
   if (pool->tids == NULL) {
      error("alloc_cmp_pool: malloc() error.\n");
      free(pool);
      return NULL;
   }

   MS_MUTEX_INIT(&pool->lock);
   MS_COND_INIT(&pool->work);
   MS_COND_INIT(&pool->done);

   pool->n_workers = n_workers;
   pool->max_inflight = n_workers * 2;

   for (i = 0; i < n_workers; i++) {
#ifdef _WIN32
      pool->tids[i] =
          CreateThread(NULL, 0, cmp_pool_worker_win, (LPVOID)pool, 0, NULL);
      if (pool->tids[i] == NULL) {
         perror("CreateThread");
         exit(-1);
      }
#else
      int ret =
          pthread_create(&pool->tids[i], NULL, cmp_pool_worker, (void*)pool);
      if (ret != 0) {
         perror("pthread_create");
         exit(-1);
      }
#endif
   }

   return pool;
}

/**
 * @brief Shuts down a compression worker pool, joins its threads and frees it.
 * @param pool The pool to deallocate. `NULL` is a no-op (serial path).
 * @warning Must not be called while a pass is in flight.
 */
static void dealloc_cmp_pool(cmp_pool_t* pool) {
   int i;

   if (pool == NULL)
      return;

   MS_MUTEX_LOCK(&pool->lock);
   pool->shutdown = 1;
   MS_COND_BROADCAST(&pool->work);
   MS_MUTEX_UNLOCK(&pool->lock);

   for (i = 0; i < pool->n_workers; i++) {
#ifdef _WIN32
      WaitForSingleObject(pool->tids[i], INFINITE);
      CloseHandle(pool->tids[i]);
#else
      pthread_join(pool->tids[i], NULL);
#endif
   }

   MS_COND_DESTROY(&pool->done);
   MS_COND_DESTROY(&pool->work);
   MS_MUTEX_DESTROY(&pool->lock);

   free(pool->tids);
   free(pool);
}

/*
 * ===========================================================================
 * Speculative pre-compression
 * ===========================================================================
 *
 * The preprocess scan (scan_mzml) is single-threaded and completes before any
 * compression starts, leaving every worker idle for its duration. It does not
 * have to: the division layout that create_divisions() will produce is a pure
 * function of values that are all known *before* the scan runs —
 *
 *   n_divisions   = determine_n_divisions(input_filesize, blocksize), clamped
 *                   against arguments->threads and the spectrum count
 *   n_spec_per_div = total_spec / n_divisions
 *
 * — because div->size is just the input filesize and div->mz->total_spec is
 * df->source_total_spec, which pattern_detect() reads from the mzML's
 * <spectrumList count="..."> attribute. create_divisions() then slices purely
 * by spectrum index, so division `d` covers spectra [d*n, (d+1)*n) and XML
 * entries [2*d*n, 2*(d+1)*n). Every one of those positions is final as soon as
 * the scan has walked past that division's last spectrum.
 *
 * So we predict the layout, and as the scan reports progress the pool
 * compresses each (stream, division) it can already see. The prediction is
 * *not* trusted: scan_mzml lowers source_total_spec when a file turns out to
 * hold fewer spectra than it claimed, which shifts every boundary. Rather than
 * reason about when that can happen, spec_take() compares the positions that
 * were actually compressed against the real ones create_divisions() produced,
 * element by element, and only reuses a result on an exact match. A compressed
 * block is a pure function of its positions, the input map and the data
 * format, so equal positions guarantee equal bytes; a mispredicted layout
 * simply falls back to compressing normally.
 *
 * Nothing is written to the output fd during speculation — the header is still
 * the first thing written, at the same point as before — so a mispredict costs
 * only wasted CPU and can never produce a partial or wrong file.
 *
 * Only the uniform divisions [0, n_divisions-1) are speculated on. The last
 * division (which absorbs the leftover spectra) and the trailing XML-only
 * division both depend on the end of the scan, so there is nothing to gain and
 * their edge cases are avoided entirely.
 */

/* Streams speculated on, in the order compress_mzml writes them. */
#define SPEC_N_STREAMS 3

typedef struct {
   data_positions_t dp;     /* Owned copy of the predicted positions. */
   cmp_blk_queue_t* result; /* Compressed blocks, NULL until done/after take. */
} spec_task_t;

struct spec_ctx {
   char* input_map;
   data_format_t df; /* Private snapshot; the scan mutates the caller's. */
   long blocksize;   /* Pre-rewrite; affects buffer sizing only, not output. */

   long n_spec_per_div;
   int n_tasks; /* Division-major: task index = division * 3 + stream. */
   spec_task_t* tasks;

   cmp_pool_t* pool; /* Borrowed; owned by this context, see spec_start(). */

   /* Guarded by pool->lock. */
   data_positions_t* xml_dp;
   data_positions_t* mz_dp;
   data_positions_t* inten_dp;
   int scanned;    /* Spectra fully recorded by the scan so far. */
   int active;     /* Cleared once no further tasks may be claimed. */
   int next_task;  /* Next task index a worker may claim. */
   int n_running;  /* Claimed but not yet finished. */
};

/**
 * @brief Number of scanned spectra required before task `idx` can be claimed.
 */
static long spec_task_requires(spec_ctx_t* spec, int idx) {
   return ((long)(idx / SPEC_N_STREAMS) + 1) * spec->n_spec_per_div;
}

/**
 * @brief Whether a speculative task is claimable right now. Lock held.
 */
static int spec_ready_locked(spec_ctx_t* spec) {
   if (spec == NULL || !spec->active || spec->xml_dp == NULL)
      return 0;
   if (spec->next_task >= spec->n_tasks)
      return 0;
   return spec->scanned >= spec_task_requires(spec, spec->next_task);
}

/**
 * @brief Claims the next speculative task and returns its index. Lock held.
 */
static int spec_claim_locked(spec_ctx_t* spec) {
   spec->n_running++;
   return spec->next_task++;
}

/**
 * @brief Records completion of a claimed speculative task. Lock held.
 */
static void spec_complete_locked(spec_ctx_t* spec) { spec->n_running--; }

/**
 * @brief Compresses one predicted (stream, division) slice.
 *
 * Copies the predicted positions out of the scan's arrays (so the result stays
 * valid and comparable independently of the scan's own bookkeeping) and runs
 * the ordinary `compress_routine()` over them.
 *
 * @param spec The speculative context.
 * @param idx Task index, division-major.
 */
static void spec_run_task(spec_ctx_t* spec, int idx) {
   spec_task_t* task = &spec->tasks[idx];
   data_positions_t* src;
   compress_args_t args;
   long off, n;
   int division = idx / SPEC_N_STREAMS;

   switch (idx % SPEC_N_STREAMS) {
      case 0:
         src = spec->xml_dp;
         off = 2 * division * spec->n_spec_per_div;
         n = 2 * spec->n_spec_per_div;
         args.mode = _xml_;
         args.comp_fun = spec->df.xml_compression_fun;
         break;
      case 1:
         src = spec->mz_dp;
         off = division * spec->n_spec_per_div;
         n = spec->n_spec_per_div;
         args.mode = _mass_;
         args.comp_fun = spec->df.mz_compression_fun;
         break;
      default:
         src = spec->inten_dp;
         off = division * spec->n_spec_per_div;
         n = spec->n_spec_per_div;
         args.mode = _intensity_;
         args.comp_fun = spec->df.inten_compression_fun;
         break;
   }

   task->dp.start_positions = malloc(sizeof(uint64_t) * n);
   task->dp.end_positions = malloc(sizeof(uint64_t) * n);

   if (task->dp.start_positions == NULL || task->dp.end_positions == NULL) {
      free(task->dp.start_positions);
      free(task->dp.end_positions);
      task->dp.start_positions = NULL;
      task->dp.end_positions = NULL;
      return; /* Leaves result NULL; the division is compressed normally. */
   }

   memcpy(task->dp.start_positions, src->start_positions + off,
          sizeof(uint64_t) * n);
   memcpy(task->dp.end_positions, src->end_positions + off,
          sizeof(uint64_t) * n);
   task->dp.total_spec = (int)n;
   task->dp.file_end = 0;

   args.input_map = spec->input_map;
   args.dp = &task->dp;
   args.df = &spec->df;
   args.cmp_blk_size = spec->blocksize;
   args.blocksize = spec->blocksize / 3;
   args.ret = NULL;

   compress_routine((void*)&args);

   task->result = args.ret;
}

/**
 * @brief Starts speculative pre-compression for an mzML about to be scanned.
 *
 * Predicts the division layout create_divisions() will produce, allocates the
 * worker pool that `compress_mzml_spec()` will go on to reuse for its three
 * ordered passes, and leaves the workers waiting on scan progress.
 *
 * @param arguments The parsed runtime arguments (threads, blocksize, lossy).
 * @param input_map The memory-mapped input mzML file.
 * @param input_filesize Size of the input file in bytes.
 * @param df The format detected by `pattern_detect()`.
 * @return A speculative context, or `NULL` if speculation does not apply (too
 *         few threads or divisions, no usable spectrum count, or an invalid
 *         compression configuration). `NULL` is not an error.
 * @note The caller owns the result and must free it with `dealloc_spec_ctx()`.
 */
spec_ctx_t* spec_start(Arguments* arguments, char* input_map,
                       long input_filesize, data_format_t* df) {
   spec_ctx_t* spec;
   long total_spec, n_divisions;
   int n_workers, n_spec_divisions;

   if (arguments == NULL || df == NULL || input_map == NULL)
      return NULL;

   total_spec = df->source_total_spec;

   if (arguments->threads < 2 || total_spec < 2 || input_filesize <= 0)
      return NULL;

   /* Mirror preprocess_mzml's division-count decision. A wrong guess here is
      caught by spec_take()'s comparison, so this only has to be right often,
      not always. */
   n_divisions = determine_n_divisions(input_filesize, arguments->blocksize);
   if (n_divisions > total_spec)
      n_divisions = total_spec;
   if (n_divisions < arguments->threads) {
      n_divisions = arguments->threads;
      if (n_divisions > total_spec)
         n_divisions = total_spec;
   }

   n_spec_divisions = (int)n_divisions - 1; /* Skip the leftover division. */
   if (n_spec_divisions < 1)
      return NULL;

   spec = calloc(1, sizeof(spec_ctx_t));
   if (spec == NULL) {
      error("spec_start: calloc() error.\n");
      return NULL;
   }

   spec->df = *df; /* Snapshot: scan_mzml may lower source_total_spec. */
   if (set_compress_runtime_variables(arguments, &spec->df)) {
      free(spec);
      return NULL; /* compress_mzml will report the error for real. */
   }

   n_workers = arguments->threads < (int)n_divisions ? arguments->threads
                                                     : (int)n_divisions;
   spec->pool = alloc_cmp_pool(n_workers);
   if (spec->pool == NULL) {
      free(spec);
      return NULL;
   }

   spec->input_map = input_map;
   spec->blocksize = arguments->blocksize;
   spec->n_spec_per_div = total_spec / n_divisions;

   /* Cap the speculative run at the pool's in-flight budget so the compressed
      bytes held before the first write stay bounded, as in compress_parallel. */
   spec->n_tasks = n_spec_divisions * SPEC_N_STREAMS;
   if (spec->n_tasks > spec->pool->max_inflight)
      spec->n_tasks = spec->pool->max_inflight;

   spec->tasks = calloc(spec->n_tasks, sizeof(spec_task_t));
   if (spec->tasks == NULL) {
      error("spec_start: calloc() error.\n");
      dealloc_cmp_pool(spec->pool);
      free(spec);
      return NULL;
   }

   MS_MUTEX_LOCK(&spec->pool->lock);
   spec->active = 1;
   spec->pool->spec = spec;
   MS_MUTEX_UNLOCK(&spec->pool->lock);

   return spec;
}

/**
 * @brief `scan_progress_cb` that publishes scan progress to the workers.
 *
 * Only takes the lock on division boundaries: a worker's readiness changes
 * only when `n_scanned` reaches a multiple of `n_spec_per_div`, so publishing
 * every spectrum would be pure lock traffic.
 */
void spec_scan_publish(void* ctx, data_positions_t* xml_dp,
                       data_positions_t* mz_dp, data_positions_t* inten_dp,
                       int n_scanned) {
   spec_ctx_t* spec = (spec_ctx_t*)ctx;

   if (spec == NULL)
      return;

   if (n_scanned % spec->n_spec_per_div != 0)
      return;

   MS_MUTEX_LOCK(&spec->pool->lock);
   if (spec->xml_dp == NULL) {
      /* Published once, on the first call. Workers read these without the lock
         once they have seen a non-NULL xml_dp, so they must never be rewritten. */
      spec->xml_dp = xml_dp;
      spec->mz_dp = mz_dp;
      spec->inten_dp = inten_dp;
   }
   spec->scanned = n_scanned;
   MS_COND_BROADCAST(&spec->pool->work);
   MS_MUTEX_UNLOCK(&spec->pool->lock);
}

/**
 * @brief Ends speculation and waits for any in-flight task to finish.
 *
 * Called once the scan is over — successfully or not — so no worker is left
 * waiting for progress that will never arrive. Idempotent.
 *
 * @param spec The speculative context, or `NULL`.
 */
void spec_scan_finished(spec_ctx_t* spec) {
   if (spec == NULL)
      return;

   MS_MUTEX_LOCK(&spec->pool->lock);
   spec->active = 0;
   MS_COND_BROADCAST(&spec->pool->work);
   while (spec->n_running > 0) MS_COND_WAIT(&spec->pool->done, &spec->pool->lock);
   spec->pool->spec = NULL;
   MS_MUTEX_UNLOCK(&spec->pool->lock);
}

/**
 * @brief Claims a speculative result for a division, if it is provably usable.
 *
 * Compares the positions that were actually compressed against the real ones
 * produced by `create_divisions()`. Only an exact match is reused, which makes
 * the output byte-identical to compressing the division here and now.
 *
 * @param spec The speculative context, or `NULL`.
 * @param mode The stream being compressed (`_xml_`, `_mass_`, `_intensity_`).
 * @param division The division index within the current pass.
 * @param real_dp The authoritative data positions for that division.
 * @return The compressed block queue (ownership transferred), or `NULL` if
 *         nothing was speculated for it or the prediction did not hold.
 */
static cmp_blk_queue_t* spec_take(spec_ctx_t* spec, int mode, int division,
                                  data_positions_t* real_dp) {
   spec_task_t* task;
   cmp_blk_queue_t* result;
   size_t bytes;
   int stream, idx;

   if (spec == NULL || real_dp == NULL)
      return NULL;

   if (mode == _xml_)
      stream = 0;
   else if (mode == _mass_)
      stream = 1;
   else
      stream = 2;

   idx = division * SPEC_N_STREAMS + stream;
   if (idx < 0 || idx >= spec->n_tasks)
      return NULL;

   task = &spec->tasks[idx];

   if (task->result == NULL || task->dp.total_spec != real_dp->total_spec)
      return NULL;

   bytes = sizeof(uint64_t) * (size_t)real_dp->total_spec;

   if (memcmp(task->dp.start_positions, real_dp->start_positions, bytes) != 0 ||
       memcmp(task->dp.end_positions, real_dp->end_positions, bytes) != 0)
      return NULL;

   result = task->result;
   task->result = NULL;

   return result;
}

/**
 * @brief The worker pool owned by a speculative context, for reuse by the
 *        ordered compression passes.
 * @param spec The speculative context, or `NULL`.
 * @return The pool, or `NULL`.
 */
static cmp_pool_t* spec_pool(spec_ctx_t* spec) {
   return spec ? spec->pool : NULL;
}

/**
 * @brief Shuts down speculation and frees the context and its worker pool.
 * @param spec The speculative context, or `NULL`.
 */
void dealloc_spec_ctx(spec_ctx_t* spec) {
   int i;

   if (spec == NULL)
      return;

   spec_scan_finished(spec); /* No-op if already ended. */

   for (i = 0; i < spec->n_tasks; i++) {
      if (spec->tasks[i].result) /* Unclaimed: the prediction did not hold. */
         dealloc_cmp_buff(spec->tasks[i].result);
      free(spec->tasks[i].dp.start_positions);
      free(spec->tasks[i].dp.end_positions);
   }

   dealloc_cmp_pool(spec->pool);
   free(spec->tasks);
   free(spec);
}

/**
 * @brief Compresses one stream across all divisions, writing blocks in order.
 *
 * Hands the divisions to the persistent worker pool and then acts as the
 * ordered writer on the calling thread: for each division in turn it waits for
 * that division to finish compressing, appends its blocks to `blk_len_queue`
 * and writes them via `cmp_dump`, then releases the next slot to the workers.
 * Writing division `i` therefore overlaps the compression of divisions
 * `i+1..`, replacing the previous per-wave join-then-dump barrier.
 *
 * If `pool` is `NULL` (threads <= 1) each division is compressed inline on the
 * calling thread immediately before being written.
 *
 * @param pool The persistent worker pool, or `NULL` for the serial path.
 * @param input_map The memory-mapped input mzML file.
 * @param ddp An array of `data_positions_t` pointers, one per division.
 * @param df A pointer to the `data_format_t` struct with compression settings.
 * @param comp_fun A function pointer to the compression function (e.g., `zstd_compress`).
 * @param cmp_blk_size The target size for compression blocks.
 * @param blocksize The size of the data block buffer for accumulating data.
 * @param mode The stream type to compress (`_xml_`, `_mass_`, or `_intensity_`).
 * @param divisions The total number of divisions to process.
 * @param fd The output file descriptor to write compressed data to.
 * @param bytes_written Optional out-parameter. If non-NULL, receives the total number
 *        of compressed bytes written to the file descriptor.
 * @return A pointer to the `block_len_queue_t` containing block length metadata for all
 *         compressed blocks, or `NULL` on error. The caller is responsible for freeing
 *         this via `dealloc_block_len_queue()`.
 * @note Each `compress_args_t` and its compressed output are freed after writing to disk.
 */
static block_len_queue_t* compress_parallel(
    cmp_pool_t* pool, spec_ctx_t* spec, char* input_map,
    data_positions_t** ddp, data_format_t* df, compression_fun comp_fun,
    size_t cmp_blk_size, long blocksize, int mode, int divisions, int fd,
    size_t* bytes_written) {
   block_len_queue_t* blk_len_queue;
   compress_args_t** args;
   char* done_flags = NULL;
   size_t total_written = 0;
   int failed = 0;
   int reused = 0;
   int i;

   args = malloc(sizeof(compress_args_t*) * divisions);
   if (args == NULL) {
      error("compress_parallel: malloc() error.\n");
      return NULL;
   }

   if (pool) {
      done_flags = calloc(divisions, sizeof(char));
      if (done_flags == NULL) {
         error("compress_parallel: calloc() error.\n");
         free(args);
         return NULL;
      }
   }

   for (i = 0; i < divisions; i++) {
      args[i] = alloc_compress_args(input_map, ddp[i], df, comp_fun,
                                    cmp_blk_size, blocksize, mode);
      if (args[i] == NULL) {
         error("compress_parallel: Failed to allocate compress_args_t.\n");
         while (--i >= 0) dealloc_compress_args(args[i]);
         free(done_flags);
         free(args);
         return NULL;
      }

      /* Adopt a speculative result if it was built from the very same
         positions; otherwise this division is compressed as usual. */
      args[i]->ret = spec_take(spec, mode, i, ddp[i]);
      if (args[i]->ret) {
         reused++;
         if (done_flags)
            done_flags[i] = 1;
      }
   }

   if (reused)
      print("\t%d/%d divisions taken from speculative pre-compression.\n",
            reused, divisions);

   if (pool) {
      /* Publish the pass; workers start claiming divisions immediately. */
      MS_MUTEX_LOCK(&pool->lock);
      pool->args = args;
      pool->done_flags = done_flags;
      pool->n_divisions = divisions;
      pool->next_task = 0;
      pool->next_write = 0;
      MS_COND_BROADCAST(&pool->work);
      MS_MUTEX_UNLOCK(&pool->lock);
   }

   blk_len_queue = alloc_block_len_queue();

   for (i = 0; i < divisions; i++) {
      if (pool == NULL) {
         if (args[i]->ret == NULL) /* Serial path (threads <= 1). */
            compress_routine((void*)args[i]);
      } else {
         MS_MUTEX_LOCK(&pool->lock);
         while (!done_flags[i]) MS_COND_WAIT(&pool->done, &pool->lock);
         MS_MUTEX_UNLOCK(&pool->lock);
      }

      /* On failure keep draining so no worker is left holding a division. */
      if (!failed) {
         size_t written = cmp_dump(args[i]->ret, blk_len_queue, fd);
         if (written == (size_t)-1)
            failed = 1;
         else
            total_written += written;
      }

      dealloc_compress_args(args[i]);

      if (pool) {
         MS_MUTEX_LOCK(&pool->lock);
         pool->next_write = i + 1;
         MS_COND_BROADCAST(&pool->work);
         MS_MUTEX_UNLOCK(&pool->lock);
      }
   }

   if (pool) {
      /* Park the workers until the next pass is published. */
      MS_MUTEX_LOCK(&pool->lock);
      pool->args = NULL;
      pool->done_flags = NULL;
      pool->n_divisions = 0;
      pool->next_task = 0;
      pool->next_write = 0;
      MS_MUTEX_UNLOCK(&pool->lock);
      free(done_flags);
   }

   free(args);

   if (failed) {
      dealloc_block_len_queue(blk_len_queue);
      return NULL;
   }

   if (bytes_written)
      *bytes_written = total_written;

   return blk_len_queue;
}

/**
 * @brief Main entry point for compressing an mzML file into the MSZ format.
 *
 * Orchestrates the full compression pipeline: sets runtime compression variables,
 * writes the MSZ header, compresses XML/m/z/intensity streams in parallel,
 * writes block length metadata, division metadata, and the footer to the output file.
 *
 * @param input_map The memory-mapped input mzML file.
 * @param input_filesize The size in bytes of the input mzML file.
 * @param arguments A pointer to the parsed command-line `Arguments` struct.
 * @param df A pointer to the `data_format_t` struct with source format and compression settings.
 * @param divisions A pointer to the `divisions_t` struct describing how the input was partitioned.
 * @param output_fd The file descriptor for the output .msz file.
 * @return 0 on success, -1 on error.
 * @note This function writes the complete MSZ file (header, compressed streams,
 *       block lengths, divisions, footer). The caller is responsible for opening
 *       and closing the file descriptors.
 */
int compress_mzml(char* input_map, size_t input_filesize, Arguments* arguments,
                  data_format_t* df, divisions_t* divisions, int output_fd) {
   return compress_mzml_spec(input_map, input_filesize, arguments, df,
                             divisions, output_fd, NULL);
}

/**
 * @brief `compress_mzml()` that can adopt speculative pre-compression results.
 *
 * Identical to `compress_mzml()`, but reuses the worker pool created by
 * `spec_start()` and, for every division, adopts the speculatively compressed
 * blocks when the positions they were built from match the real ones exactly.
 * Divisions without a usable speculative result are compressed normally, so
 * the output is byte-identical either way.
 *
 * @param spec The speculative context from `preprocess_mzml_spec()`, or `NULL`
 *             for the plain behaviour. Ownership stays with the caller.
 */
int compress_mzml_spec(char* input_map, size_t input_filesize,
                       Arguments* arguments, data_format_t* df,
                       divisions_t* divisions, int output_fd,
                       spec_ctx_t* spec) {
   // Initialize footer to all 0's to not write garbage to file.
   footer_t* footer = calloc(1, sizeof(footer_t));

   block_len_queue_t *xml_block_lens, *mz_binary_block_lens,
       *inten_binary_block_lens;

   data_positions_t **xml_divisions = join_xml(divisions),
                    **mz_divisions = join_mz(divisions),
                    **inten_divisions = join_inten(divisions);

   double start, end;
   size_t output_pos = 0;
   size_t stream_bytes = 0;

   start = get_time();

   if (set_compress_runtime_variables(
           arguments, df)) {  // Set compression variables (e.g. compression
                              // level, compression function, etc.)
      error("compress_mzml: Failed to set compression runtime variables.\n");
      free(footer);
      free(xml_divisions);
      free(mz_divisions);
      free(inten_divisions);
      return -1;
   }

   // Store format integer in footer.
   footer->mz_fmt = get_algo_type(arguments->mz_lossy);
   footer->inten_fmt = get_algo_type(arguments->int_lossy);

   long blocksize = arguments->blocksize;
   int threads = arguments->threads;

   /* One worker pool, reused across the XML, m/z and intensity passes. NULL
      when a single worker would be used, which selects the serial path. When
      speculation ran, its pool is reused here rather than started afresh. */
   int n_workers =
       threads < divisions->n_divisions ? threads : divisions->n_divisions;
   int owns_pool = (spec == NULL);
   cmp_pool_t* pool;

   if (spec) {
      spec_scan_finished(spec); /* No-op if preprocess already ended it. */
      pool = spec_pool(spec);
   } else
      pool = alloc_cmp_pool(n_workers);

   // Write df header to file.
   output_pos += write_header(output_fd, df, blocksize,
                              "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");

   print("\nDecoding and compression...\n");

   print("\t===XML===\n");
   footer->xml_pos = output_pos;
   xml_block_lens = compress_parallel(
       pool, spec, (char*)input_map, xml_divisions, df, df->xml_compression_fun,
       blocksize, blocksize / 3, _xml_, divisions->n_divisions, output_fd,
       &stream_bytes); /* Compress XML */
   output_pos += stream_bytes;
   free(xml_divisions);
   if (xml_block_lens == NULL) {
      error("compress_mzml: Failed to compress XML stream.\n");
      if (owns_pool)
         dealloc_cmp_pool(pool);
      free(footer);
      free(mz_divisions);
      free(inten_divisions);
      return -1;
   }

   print("\t===m/z binary===\n");
   footer->mz_binary_pos = output_pos;
   mz_binary_block_lens = compress_parallel(
       pool, spec, (char*)input_map, mz_divisions, df, df->mz_compression_fun,
       blocksize, blocksize / 3, _mass_, divisions->n_divisions, output_fd,
       &stream_bytes); /* Compress m/z binary */
   output_pos += stream_bytes;
   free(mz_divisions);
   if (mz_binary_block_lens == NULL) {
      error("compress_mzml: Failed to compress m/z binary stream.\n");
      if (owns_pool)
         dealloc_cmp_pool(pool);
      dealloc_block_len_queue(xml_block_lens);
      free(footer);
      free(inten_divisions);
      return -1;
   }

   print("\t===int binary===\n");
   footer->inten_binary_pos = output_pos;
   inten_binary_block_lens = compress_parallel(
       pool, spec, (char*)input_map, inten_divisions, df,
       df->inten_compression_fun,
       blocksize, blocksize / 3, _intensity_, divisions->n_divisions, output_fd,
       &stream_bytes); /* Compress int binary */
   output_pos += stream_bytes;
   free(inten_divisions);
   if (inten_binary_block_lens == NULL) {
      error("compress_mzml: Failed to compress intensity binary stream.\n");
      if (owns_pool)
         dealloc_cmp_pool(pool);
      dealloc_block_len_queue(xml_block_lens);
      dealloc_block_len_queue(mz_binary_block_lens);
      free(footer);
      return -1;
   }

   if (owns_pool) /* All passes done; workers are no longer needed. */
      dealloc_cmp_pool(pool);

   // Dump block_len_queue to msz file.
   footer->xml_blk_pos = output_pos;
   output_pos += dump_block_len_queue(xml_block_lens, output_fd);

   footer->mz_binary_blk_pos = output_pos;
   output_pos += dump_block_len_queue(mz_binary_block_lens, output_fd);

   footer->inten_binary_blk_pos = output_pos;
   output_pos += dump_block_len_queue(inten_binary_block_lens, output_fd);

   // Write divisions to file.
   footer->divisions_t_pos = output_pos;
   output_pos += write_divisions(divisions, output_fd);

   // Write footer to file.
   footer->original_filesize = input_filesize;
   footer->n_divisions =
       divisions->n_divisions;  // Set number of divisions in footer.
   footer->num_spectra =
       df->source_total_spec;  // Set number of spectra in footer.

   write_footer(footer, output_fd);

   free(footer);

   end = get_time();

   print("Decoding and compression time: %1.4fs\n", end - start);

   return 0;
}

/**
 * @brief Sets the compression function based on the accession integer.
 * @param accession An integer representing the compression type.
 * @return A function pointer to the corresponding compression function on
 * success. `NULL` on error.
 */
compression_fun set_compress_fun(int accession) {
   switch (accession) {
      case _ZSTD_compression_:
         return zstd_compress;
      case _LZ4_compression_:
         return lz4_compress;
      case _no_comp_:
         return no_compress;
      default:
         error("Compression type not supported.");
         return NULL;
   }
}

/**
 * @brief Gets the compression type accession integer based on the string
 * argument.
 * @param arg A string representing the compression type ("zstd", "lz4",
 * "nocomp", "none").
 * @return An integer representing the compression type on success. -1 on error.
 */
int get_compress_type(char* arg) {
   if (arg == NULL) {
      error("Compression type not specified.");
      return -1;
   }
   if (strcmp(arg, "zstd") == 0 || strcmp(arg, "ZSTD") == 0)
      return _ZSTD_compression_;
   if (strcmp(arg, "lz4") == 0 || strcmp(arg, "LZ4") == 0)
      return _LZ4_compression_;
   if (strcmp(arg, "nocomp") == 0 || strcmp(arg, "none") == 0)
      return _no_comp_;
}