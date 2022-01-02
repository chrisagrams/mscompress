#include "mscompress.h"
#include <stdlib.h>

cmp_blk_queue_t*
alloc_cmp_buff()
/**
 * @brief Allocates a cmp_blk_queue struct that represents a double-linked-list 
 * of compressed blocks (cmp_block_t) to write to disk as a LIFO queue.
 * 
 * @return An allocated cmp_blk_queue_t struct.
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
/**
 * @brief Deallocates a cmp_blk_queue and all of its children.
 * Traverses through double-linked-list and frees all cmp_block_t.
 * 
 * @param queue A cmp_blk_queue_t to deallocate
 * 
 * @return None.
 * 
 */
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
append_cmp_block(cmp_blk_queue_t* queue, cmp_block_t* blk)
/**
 * @brief Append a compressed block to the queue.
 * Appends to end of double-linked-list in O(1).
 * 
 * @param queue A cmp_blk_queue_t queue.
 * 
 * @param blk A compressed block to append.
 * 
 * @return None.
 * 
 */
{
    cmp_block_t* old_tail;

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

cmp_block_t*
pop_cmp_block(cmp_blk_queue_t* queue)
/**
 * @brief Removes a compressed block from the front of the queue.
 * 
 */
{
    cmp_block_t* old_head;

    old_head = queue->head;
    
    if(queue->head == queue->tail)
    {
        queue->head = NULL;
        queue->tail = NULL;
    }
    else
        queue->head = old_head->next;

    old_head->next = NULL;

    queue->populated--;

    return old_head;

}

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