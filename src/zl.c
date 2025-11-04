#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/zlib/zlib.h"
#include "mscompress.h"
// #define ZLIB_BUFF_FACTOR 100

zlib_block_t* zlib_alloc(int offset) {
   if (offset < 0) {
      warning("zlib_alloc: offset must be >= 0");
      return NULL;
   }

   zlib_block_t* r = malloc(sizeof(zlib_block_t));

   if (r == NULL) {
      warning("zlib_alloc: malloc error");
      return NULL;
   }
   r->len = ZLIB_BUFF_FACTOR;
   r->size = r->len + offset;
   r->offset = offset;
   r->mem = malloc(r->size);
   if (r->mem == NULL) {
      warning("zlib_alloc: malloc error");
      return NULL;
   }
   r->buff = r->mem + r->offset;

   return r;
}

void zlib_realloc(zlib_block_t* old_block, size_t new_size) {
   old_block->len = new_size;
   old_block->size = old_block->len + old_block->offset;
   old_block->mem = realloc(old_block->mem, old_block->size);
   if (!old_block->mem) {
      fprintf(stderr, "realloc() error");
      exit(1);
   }
   old_block->buff = old_block->mem + old_block->offset;
}

void zlib_dealloc(zlib_block_t* blk) {
   if (blk) {
      free(blk->mem);
      free(blk);
   }
}

int zlib_append_header(zlib_block_t* blk, void* content, size_t size) {
   if (size > blk->offset)
      return 0;
   memcpy(blk->mem, content, size);
   return 1;
}

void* zlib_pop_header(zlib_block_t* blk) {
   void* r;
   r = malloc(blk->offset);
   memcpy(r, blk->mem, blk->offset);
   return r;
}

z_stream* alloc_z_stream() {
   z_stream* z;

   z = calloc(1, sizeof(z_stream));

   if (z == NULL) {
      warning("alloc_z_stream: calloc error\n");
      return NULL;
   }
   if (deflateInit(z, Z_DEFAULT_COMPRESSION) != Z_OK) {
      warning("alloc_z_stream: deflateInit error\n");
      return NULL;
   }

   return z;
}

void dealloc_z_stream(z_stream* z) {
   if (z) {
      deflateEnd(z);
      free(z);
   }
}

uInt zlib_compress(z_stream* z, Bytef* input, zlib_block_t* output,
                   uInt input_len) {
   uInt r;

   uInt output_len = output->len;

   if (z == NULL)
      error("zlib_compress: z_stream is NULL");

   z->avail_in = input_len;
   z->next_in = input;
   z->avail_out = output_len;
   z->next_out = output->buff;
   z->total_out = 0;

   int ret;

   do {
      z->avail_out = output_len - z->total_out;
      z->next_out = output->buff + z->total_out;

      ret = deflate(z, Z_FINISH);

      if (ret != Z_OK)
         break;

      output_len += ZLIB_BUFF_FACTOR;
      zlib_realloc(output, output_len);

   } while (z->avail_out == 0);

   r = z->total_out;

   deflateReset(z);  // reset the z_stream

   zlib_realloc(output, r);  // shrink the buffer down to only what is in use

   return r;
}

uInt zlib_decompress(z_stream* z, Bytef* input, zlib_block_t* output,
                     uInt input_len) {
   uInt r;

   uInt output_len = output->len;

   if (z == NULL)
      error("zlib_decompress: z_stream is NULL");

   z->avail_in = input_len;
   z->next_in = input;
   z->avail_out = output_len;
   z->next_out = output->buff;
   z->total_out = 0;

   inflateInit(z);

   int ret;

   do {
      z->avail_out = output_len - z->total_out;
      z->next_out = output->buff + z->total_out;

      ret = inflate(z, Z_NO_FLUSH);

      if (ret != Z_OK)
         break;

      output_len += ZLIB_BUFF_FACTOR;
      zlib_realloc(output, output_len);

   } while (z->avail_out == 0);

   r = z->total_out;

   inflateReset(z);

   zlib_realloc(output, r);  // shrink the buffer down to only what is in use

   return r;
}
