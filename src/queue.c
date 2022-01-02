#include "mscompress.h"
#include <stdlib.h>

block_len_t*
alloc_block_len(size_t original_size, size_t compressed_size)
{
    block_len_t* r;

    r = (block_len_t*)malloc(sizeof(block_len_t*));

    r->original_size = original_size;
    r->compresssed_size = compressed_size;
    r->next = NULL;

    return r;
}

void
dealloc_block_len(block_len_t* blk)
{
    if(blk)
    {
        free(blk);
    }
}

block_len_queue_t*
alloc_block_len_queue()
{
    block_len_queue_t* r;

    r = (block_len_queue_t*)malloc(sizeof(block_len_queue_t));

    r->head = NULL;
    r->tail = NULL;
    r->populated = 0;

    return r;
}


void
dealloc_block_len_queue(block_len_queue_t* queue)
{
    if(queue)
    {
        if(queue->head)
        {
            block_len_t* curr_head = queue->head;
            block_len_t* new_head = curr_head->next;
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
append_block_len(block_len_queue_t* queue, size_t original_size, size_t compressed_size)
{
    block_len_t* old_tail;
    block_len_t* blk;

    blk = alloc_block_len(original_size, compressed_size);

    old_tail = queue->tail;
    if(old_tail)
    {
        old_tail->next = blk;
        queue->tail = blk;
    }
    else
    {
        queue->head = blk;
        queue->tail = blk;
    }
    queue->populated++;

    return;
}

void
dump_block_len_queue(block_len_queue_t* queue, int fd)
{
    block_len_t* curr;
    char buff[sizeof(size_t)];

    size_t* buff_cast = (size_t*)(&buff[0]);

    curr = queue->head;

    while(curr != NULL)
    {
        *buff_cast = curr->original_size;
        write_to_file(fd, buff, sizeof(size_t));

        *buff_cast = curr->compresssed_size;
        write_to_file(fd, buff, sizeof(size_t));

        curr = curr->next;
    }

    dealloc_block_len_queue(queue);
}

block_len_queue_t*
read_block_len_queue(void* input_map, int offset, int end)
{
    block_len_queue_t* r;
    long diff;
    int factor;
    int i;

    r = alloc_block_len_queue();

    diff = end - offset;

    factor = sizeof(size_t)*2;

    input_map += offset;

    for(i = 0; i < diff; i+=factor)
        append_block_len(r, *(size_t*)(&input_map[0]+i), *(size_t*)(&input_map[0]+i+sizeof(size_t)));

    return r;
}