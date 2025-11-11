#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/zlib/zlib.h"
#include "mscompress.h"


/**
 * @brief Allocates a `zlib_block_t` struct with the specified offset and initializes its fields.
 * @param offset The offset to use for the buffer. Must be non-negative.
 * @return A pointer to the allocated `zlib_block_t` struct on success, NULL on error.
 */
zlib_block_t* zlib_alloc(int offset) {
   if (offset < 0) {
      error("zlib_alloc: offset must be >= 0");
      return NULL;
   }

   zlib_block_t* r = malloc(sizeof(zlib_block_t));

   if (r == NULL) {
      error("zlib_alloc: malloc error");
      return NULL;
   }
   r->len = ZLIB_BUFF_FACTOR;
   r->size = r->len + offset;
   r->offset = offset;
   r->mem = malloc(r->size);
   if (r->mem == NULL) {
      error("zlib_alloc: malloc error");
      return NULL;
   }
   r->buff = r->mem + r->offset;

   return r;
}

/**
 * @brief Reallocates a `zlib_block_t` struct to a new size.
 * @param old_block A pointer to the `zlib_block_t` struct to be reallocated.
 * @param new_size The new size for the block. Must be non-negative.
 * @return 0 on success, 1 on error.
 */
int zlib_realloc(zlib_block_t* old_block, size_t new_size) {
   old_block->len = new_size;
   old_block->size = old_block->len + old_block->offset;
   old_block->mem = realloc(old_block->mem, old_block->size);
   if (!old_block->mem) {
      error("zlib_realloc: realloc error");
      return 1;
   }
   old_block->buff = old_block->mem + old_block->offset;
   return 0;
}

/**
 * @brief Deallocates a `zlib_block_t` struct and its fields. Frees the memory allocated for the struct and its fields.
 * @param blk A pointer to the `zlib_block_t` struct to be deallocated.
 */
void zlib_dealloc(zlib_block_t* blk) {
   if (blk) {
      free(blk->mem);
      free(blk);
   }
}

/**
 * @brief Appends a buffer to a `zlib_block_t` struct, reallocating if necessary.
 * @param data_block A pointer to the `zlib_block_t` struct to append to.
 * @param mem A pointer to the buffer to append.
 * @param buff_len The length of the buffer to append.
 * @return 0 on success, 1 on error.
 */
int zlib_append_header(zlib_block_t* blk, void* content, size_t size) {
   if (size > blk->offset)
      return 1;  // Not enough space in header to append content
   memcpy(blk->mem, content, size);
   return 0;
}


/**
 * @brief Pops the header from a `zlib_block_t` struct and returns it as a void pointer.
 * @param blk A pointer to the `zlib_block_t` struct to pop the header from.
 * @return A pointer to the header content on success, NULL on error.
 */
void* zlib_pop_header(zlib_block_t* blk) {
   void* r;
   r = malloc(blk->offset);
   if (r == NULL) {
      error("zlib_pop_header: malloc error");
      return NULL;
   }
   memcpy(r, blk->mem, blk->offset);
   return r;
}

/**
* @brief Compresses a buffer using zlib and returns the compressed buffer on success, NULL on error.
* @return A pointer to the compressed buffer on success. NULL on error.
*/
z_stream* alloc_z_stream() {
   z_stream* z;

   z = calloc(1, sizeof(z_stream));

   if (z == NULL) {
      error("alloc_z_stream: calloc error\n");
      return NULL;
   }
   if (deflateInit(z, Z_DEFAULT_COMPRESSION) != Z_OK) {
      error("alloc_z_stream: deflateInit error\n");
      return NULL;
   }

   return z;
}

/**
 * @brief Deallocates a `z_stream` struct and its fields. Frees the memory allocated for the struct and its fields.
 * @param z A pointer to the `z_stream` struct to be deallocated.
 */
void dealloc_z_stream(z_stream* z) {
   if (z) {
      deflateEnd(z);
      free(z);
   }
}


/**
 * @brief Reallocates a `zlib_block_t` struct to a new size.
 * @param old_block A pointer to the `zlib_block_t` struct to be reallocated.
 * @param new_size The new size for the block. Must be non-negative.
 * @return 0 on error, output size on success.
 */
uInt zlib_compress(z_stream* z, Bytef* input, zlib_block_t* output,
                   uInt input_len) {
   uInt r;

   uInt output_len = output->len;

   if (z == NULL) {
      error("zlib_compress: z_stream is NULL");
      return 0;
   }

   z->avail_in = input_len;
   z->next_in = input;
   z->avail_out = output_len;
   z->next_out = output->buff;
   z->total_out = 0;

   int ret;
   int zlib_realloc_ret;

   do {
      z->avail_out = output_len - z->total_out;
      z->next_out = output->buff + z->total_out;

      ret = deflate(z, Z_FINISH);

      if (ret != Z_OK)
         break;

      output_len += ZLIB_BUFF_FACTOR;
      zlib_realloc_ret = zlib_realloc(output, output_len);
      if (zlib_realloc_ret != 0) {
         error("zlib_compress: zlib_realloc error\n");
         return 0;
      }

   } while (z->avail_out == 0);

   r = z->total_out;

   deflateReset(z);  // reset the z_stream

   zlib_realloc_ret = zlib_realloc(output, r);  // shrink the buffer down to only what is in use

   if (zlib_realloc_ret != 0) {
      error("zlib_compress: zlib_realloc error\n");
      return 0;
   }

   return r;
}

/**
 * @brief Decompresses a buffer using zlib and returns the decompressed buffer on success, NULL on error.
 * @param z A pointer to the `z_stream` struct for decompression.
 * @param input A pointer to the compressed input buffer.
 * @param output A pointer to the `zlib_block_t` struct to store the decompressed output.
 * @param input_len The length of the compressed input buffer.
 * @return The size of the decompressed output on success, 0 on error.
 */
uInt zlib_decompress(z_stream* z, Bytef* input, zlib_block_t* output,
                     uInt input_len) {
   uInt r;

   uInt output_len = output->len;

   if (z == NULL) {
      error("zlib_decompress: z_stream is NULL");
      return 0;
   }

   z->avail_in = input_len;
   z->next_in = input;
   z->avail_out = output_len;
   z->next_out = output->buff;
   z->total_out = 0;

   inflateInit(z);

   int ret;
   int zlib_realloc_ret;

   do {
      z->avail_out = output_len - z->total_out;
      z->next_out = output->buff + z->total_out;

      ret = inflate(z, Z_NO_FLUSH);

      if (ret != Z_OK)
         break;

      output_len += ZLIB_BUFF_FACTOR;
      zlib_realloc_ret = zlib_realloc(output, output_len);
      if (zlib_realloc_ret != 0) {
         error("zlib_decompress: zlib_realloc error\n");
         return 0;
      }

   } while (z->avail_out == 0);

   r = z->total_out;

   inflateReset(z);

   zlib_realloc_ret = zlib_realloc(output, r);  // shrink the buffer down to only what is in use

   if (zlib_realloc_ret != 0) {
      error("zlib_decompress: zlib_realloc error\n");
      return 0;
   }

   return r;
}
