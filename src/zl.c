#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>
#include "mscompress.h"
// #define ZLIB_BUFF_FACTOR 100


zlib_block_t*
zlib_alloc(int offset)
{
    zlib_block_t* r;

    r = (zlib_block_t*)malloc(sizeof(zlib_block_t));
    r->len = ZLIB_BUFF_FACTOR;
    r->size = r->len + offset;
    r->offset = offset;
    r->mem = (Bytef*)malloc(r->size);
    r->buff = r->mem + r->offset;
    
    return r;
}

void
zlib_realloc(zlib_block_t* old_block, size_t new_size)
{
    old_block->len = new_size;
    old_block->size = old_block->len + old_block->offset;
    old_block->mem = realloc(old_block->mem, old_block->size);
    if(!old_block->mem)
    {
        fprintf(stderr, "realloc() error");
        exit(1);
    }
    old_block->buff = old_block->mem + old_block->offset;
}

void
zlib_dealloc(zlib_block_t* blk)
{
    if(blk)
    {
        free(blk->mem);
        free(blk);
    }

}

int
zlib_append_header(zlib_block_t* blk, void* content, size_t size)
{
    if(size > blk->offset)
        return 0;
    memcpy(blk->mem, content, size);
    return 1;
}

void*
zlib_pop_header(zlib_block_t* blk)
{
    void* r;
    r = malloc(blk->offset);
    memcpy(r, blk->mem, blk->offset);
    return r;
}

z_stream*
alloc_z_stream()
{
    z_stream* r;

    r = (z_stream*)malloc(sizeof(z_stream));
    r->zalloc = Z_NULL;
    r->zfree = Z_NULL;
    r->opaque = Z_NULL;

    return r;
}

void
dealloc_z_stream(z_stream* stream)
{
    if(stream)
        free(stream);
}

uInt 
zlib_compress(Bytef* input, zlib_block_t* output, uInt input_len)
{
    uInt r;

    uInt output_len = output->len;
    
    z_stream* d = alloc_z_stream();
    
    d->avail_in = input_len;
    d->next_in = input;
    d->avail_out = output_len;
    d->next_out = output->buff;

    deflateInit(d, Z_DEFAULT_COMPRESSION);

    int ret;

    do 
    {
        d->avail_out = output_len-d->total_out;
        d->next_out = output->buff+d->total_out;

        ret = deflate(d, Z_FINISH);

        if(ret != Z_OK)
            break;

        output_len += ZLIB_BUFF_FACTOR;
        zlib_realloc(output, output_len);

    } while(d->avail_out == 0);

    r = d->total_out;
    
    deflateEnd(d); 
    dealloc_z_stream(d);

    zlib_realloc(output, r); // shrink the 256k buffer down to only what is in use

    return r;
} 

uInt 
zlib_decompress(Bytef* input, zlib_block_t* output, uInt input_len)
{
    uInt r;

    uInt output_len = output->len;

    z_stream* i = alloc_z_stream();

    i->avail_in = input_len;
    i->next_in = input;
    i->avail_out = output_len;
    i->next_out = output->buff;

    inflateInit(i);

    int ret;

    do
    {
        i->avail_out = output_len-i->total_out;
        i->next_out = output->buff+i->total_out;

        ret = inflate(i, Z_NO_FLUSH);
        
        if(ret != Z_OK)
            break;
    
        output_len += ZLIB_BUFF_FACTOR;
        zlib_realloc(output, output_len);

    } while (i->avail_out == 0);

    r = i->total_out;

    inflateEnd(i);
    dealloc_z_stream(i);

    zlib_realloc(output, r); // shrink the 256k buffer down to only what is in use

    return r;
}
