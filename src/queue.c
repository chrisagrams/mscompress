#include <stdio.h>
#include <stdlib.h>

#include "mscompress.h"

/**
 * @brief Allocates a compressed block queue for buffering compressed blocks to write to disk.
 *
 * Allocates and initializes a `cmp_blk_queue_t` struct representing a linked list
 * of compressed blocks (`cmp_block_t`). The queue starts empty with no head or tail.
 *
 * @return A pointer to a newly allocated `cmp_blk_queue_t` struct.
 *
 * @note The caller is responsible for freeing the returned queue via `dealloc_cmp_buff()`.
 * @warning Terminates the program if malloc fails.
 */
cmp_blk_queue_t* alloc_cmp_buff()
{
   cmp_blk_queue_t* r;

   r = malloc(sizeof(cmp_blk_queue_t));
   if (r == NULL) {
      fprintf(stderr, "alloc_cmp_buff: malloc failed\n");
      exit(-1);
   }
   r->populated = 0;
   r->head = NULL;
   r->tail = NULL;

   return r;
}

/**
 * @brief Deallocates a compressed block queue and all of its child nodes.
 *
 * Traverses the linked list from head to tail, freeing each `cmp_block_t` node,
 * then frees the queue struct itself. Safe to call with a NULL queue.
 *
 * @param queue A pointer to the `cmp_blk_queue_t` to deallocate.
 */
void dealloc_cmp_buff(cmp_blk_queue_t* queue)
{
   if (queue) {
      if (queue->head) {
         cmp_block_t* curr_head = queue->head;
         cmp_block_t* new_head = curr_head->next;
         while (new_head) {
            free(curr_head);
            curr_head = new_head;
            new_head = curr_head->next;
         }
      }
      free(queue);
   }
}

/**
 * @brief Appends a compressed block to the tail of the queue in O(1) time.
 *
 * If the queue is empty, the block becomes both the head and tail.
 * Otherwise, the block is linked after the current tail.
 *
 * @param queue A pointer to the `cmp_blk_queue_t` queue.
 * @param blk A pointer to the `cmp_block_t` to append.
 */
void append_cmp_block(cmp_blk_queue_t* queue, cmp_block_t* blk)
{
   cmp_block_t* old_tail;

   old_tail = queue->tail;
   if (old_tail) {
      old_tail->next = blk;
      queue->tail = blk;
   } else {
      queue->head = blk;
      queue->tail = blk;
   }
   queue->populated++;

   return;
}

/**
 * @brief Removes and returns the compressed block at the front of the queue.
 *
 * Removes the head node from the queue and returns it. If the queue is empty,
 * returns `NULL`. The returned block's next pointer is set to `NULL`.
 *
 * @param queue A pointer to the `cmp_blk_queue_t` queue.
 * @return A pointer to the removed `cmp_block_t`, or `NULL` if the queue is empty.
 *
 * @note The caller takes ownership of the returned block and is responsible for freeing it.
 */
cmp_block_t* pop_cmp_block(cmp_blk_queue_t* queue)
{
   cmp_block_t* old_head;

   old_head = queue->head;

   if (old_head == NULL)
      return NULL;

   if (queue->head == queue->tail) {
      queue->head = NULL;
      queue->tail = NULL;
   } else
      queue->head = old_head->next;

   old_head->next = NULL;

   queue->populated--;

   return old_head;
}

/**
 * @brief Allocates and initializes a block length tracking node.
 *
 * Creates a new block_len_t node that records the original and compressed sizes
 * of a data block. All cache fields (`cache`, `encoded_cache`, `encoded_cache_lens`)
 * are initialized to NULL/zero.
 *
 * @param original_size The uncompressed size of the data block in bytes.
 * @param compressed_size The compressed size of the data block in bytes.
 * @return A pointer to a newly allocated block_len_t struct.
 *
 * @note The caller is responsible for freeing the returned struct via `dealloc_block_len()`.
 */
block_len_t* alloc_block_len(size_t original_size, size_t compressed_size) {
   block_len_t* r;

   r = malloc(sizeof(block_len_t));

   r->original_size = original_size;
   r->compressed_size = compressed_size;
   r->next = NULL;

   r->cache = NULL;  // Set cache as "null"
   r->encoded_cache_fmt = 0;
   r->encoded_cache = NULL;
   r->encoded_cache_len = 0;
   r->encoded_cache_lens = NULL;

   return r;
}

/**
 * @brief Deallocates a `block_len_t` node and all of its owned memory.
 *
 * Frees the cache, `encoded_cache`, and `encoded_cache_lens` fields if they are
 * non-NULL, then frees the `block_len_t` struct itself. Safe to call with NULL.
 *
 * @param blk A pointer to the `block_len_t` to deallocate.
 */
void dealloc_block_len(block_len_t* blk) {
   if (blk) {
      if (blk->cache) {
         free(blk->cache);
      }
      if (blk->encoded_cache) {
         free(blk->encoded_cache);
      }
      if (blk->encoded_cache_lens) {
         free(blk->encoded_cache_lens);
      }
      free(blk);
   }
}

/**
 * @brief Allocates and initializes an empty block length queue.
 *
 * Creates a new `block_len_queue_t` struct with head and tail set to NULL
 * and populated count set to 0.
 *
 * @return A pointer to a newly allocated `block_len_queue_t` struct.
 *
 * @note The caller is responsible for freeing the returned queue via `dealloc_block_len_queue()`.
 * @warning Terminates the program if malloc fails.
 */
block_len_queue_t* alloc_block_len_queue() {
   block_len_queue_t* r;

   r = malloc(sizeof(block_len_queue_t));

   if (r == NULL) {
      fprintf(stderr, "alloc_block_len_queue: malloc failed\n");
      exit(-1);
   }

   r->head = NULL;
   r->tail = NULL;
   r->populated = 0;

   return r;
}

/**
 * @brief Deallocates a block length queue and all of its child nodes.
 *
 * Traverses the linked list from head to tail, calling dealloc_block_len()
 * on each node to free its cache and struct memory, then frees the queue
 * struct itself. Safe to call with NULL.
 *
 * @param queue A pointer to the `block_len_queue_t` to deallocate.
 */
void dealloc_block_len_queue(block_len_queue_t* queue) {
   if (queue) {
      block_len_t* curr = queue->head;
      while (curr) {
         block_len_t* next = curr->next;
         dealloc_block_len(curr);
         curr = next;
      }
      free(queue);
   }
}

/**
 * @brief Appends a new block length node to the tail of the queue.
 *
 * Allocates a new `block_len_t` node via `alloc_block_len()` with the given sizes
 * and appends it to the end of the queue. If the queue is empty, the new node
 * becomes both the head and tail.
 *
 * @param queue A pointer to the `block_len_queue_t` to append to.
 * @param original_size The uncompressed size of the data block in bytes.
 * @param compressed_size The compressed size of the data block in bytes.
 */
void append_block_len(block_len_queue_t* queue, size_t original_size,
                      size_t compressed_size) {
   block_len_t* old_tail;
   block_len_t* blk;

   blk = alloc_block_len(original_size, compressed_size);

   old_tail = queue->tail;
   if (old_tail) {
      old_tail->next = blk;
      queue->tail = blk;
   } else {
      queue->head = blk;
      queue->tail = blk;
   }
   queue->populated++;

   return;
}

/**
 * @brief Retrieves a block length node by its zero-based index in the queue.
 *
 * Traverses the linked list from the head up to the given index and returns
 * a pointer to the node at that position. Returns NULL if the index is out
 * of bounds.
 *
 * @param queue A pointer to the `block_len_queue_t` to search.
 * @param index The zero-based index of the desired node.
 * @return A pointer to the `block_len_t` at the given index, or `NULL` if not found.
 *
 * @note Returns a pointer to the existing node within the queue, not a copy.
 *       The caller must not free the returned pointer independently of the queue.
 */
block_len_t* get_block_by_index(block_len_queue_t* queue, int index) {
   block_len_t* current = queue->head;
   int i = 0;

   while (current != NULL && i < index) {
      current = current->next;
      i++;
   }

   if (current == NULL) {
      return NULL;
   }

   return current;
}

/**
 * @brief Computes the byte offset of a block at the given index within its stream section.
 *
 * Traverses the queue from the head, summing the compressed sizes of all blocks
 * before the given index to compute the cumulative byte offset. The offset is
 * relative to the start of the respective block section (XML, m/z, or intensity).
 *
 * @param queue A pointer to the `block_len_queue_t` to search.
 * @param index The zero-based index of the target block.
 * @return The byte offset of the block at the given index, or -1 if the index is out of bounds.
 */
long get_block_offset_by_index(block_len_queue_t* queue, int index)
{
   block_len_t* current = queue->head;
   int i = 0;
   long offset = 0;

   while (current != NULL && i < index) {
      offset += current->compressed_size;
      current = current->next;
      i++;
   }

   if (current == NULL) {
      return -1;
   }

   return offset;
}

/**
 * @brief Removes and returns the block length node at the front of the queue.
 *
 * Removes the head node from the queue and returns it. If the queue is empty,
 * returns `NULL`. The returned node's next pointer is set to `NULL`.
 *
 * @param queue A pointer to the `block_len_queue_t` to pop from.
 * @return A pointer to the removed `block_len_t`, or `NULL` if the queue is empty.
 *
 * @note The caller takes ownership of the returned node and is responsible
 *       for freeing it via `dealloc_block_len()`.
 */
block_len_t* pop_block_len(block_len_queue_t* queue) {
   block_len_t* old_head;

   old_head = queue->head;

   if (old_head == NULL)
      return NULL;

   if (queue->head == queue->tail) {
      queue->head = NULL;
      queue->tail = NULL;
   } else
      queue->head = old_head->next;

   old_head->next = NULL;

   queue->populated--;

   return old_head;
}

/**
 * @brief Serializes and writes a block length queue to a file descriptor, then frees the queue.
 *
 * Iterates through each `block_len_t` node in the queue, writing the original_size
 * and compressed_size as raw `size_t` values to the given file descriptor. Each node
 * is deallocated via `dealloc_block_len()` after being written. The queue struct
 * itself is freed at the end.
 *
 * @param queue A pointer to the `block_len_queue_t` to dump and deallocate.
 * @param fd The file descriptor to write the serialized data to.
 *
 * @warning This function consumes and frees the queue. The pointer is invalid after return.
 */
void dump_block_len_queue(block_len_queue_t* queue, int fd) {
   block_len_t* curr;
   block_len_t* prev;
   char buff[sizeof(size_t)];

   size_t* buff_cast = (size_t*)(&buff[0]);

   curr = queue->head;

   while (curr != NULL) {
      *buff_cast = curr->original_size;
      write_to_file(fd, buff, sizeof(size_t));

      *buff_cast = curr->compressed_size;
      write_to_file(fd, buff, sizeof(size_t));

      prev = curr;
      curr = curr->next;
      dealloc_block_len(prev);
   }

   free(queue);

   // dealloc_block_len_queue(queue);
}

/**
 * @brief Reads and reconstructs a block length queue from a memory-mapped region.
 *
 * Parses pairs of `size_t` values (`original_size`, `compressed_size`) from the memory
 * region between offset and end, creating a `block_len_t` node for each pair and
 * appending it to a newly allocated queue.
 *
 * @param input_map A pointer to the start of the memory-mapped file.
 * @param offset The byte offset from `input_map` where the block length data begins.
 * @param end The byte offset from `input_map` where the block length data ends.
 * @return A pointer to a newly allocated `block_len_queue_t` containing the parsed nodes.
 *
 * @note The caller is responsible for freeing the returned queue via `dealloc_block_len_queue()`.
 * @warning Calls `error()` and may terminate if `input_map` is `NULL` or offsets are negative.
 */
block_len_queue_t* read_block_len_queue(void* input_map, long offset,
                                        long end) {
   if (input_map == NULL)
      error("read_block_len_queue: input_map is NULL");
   if (offset < 0)
      error("read_block_len_queue: offset is negative");
   if (end < 0)
      error("read_block_len_queue: end is negative");
   block_len_queue_t* r;
   long diff;
   int factor;
   int i;

   r = alloc_block_len_queue();

   diff = end - offset;

   factor = sizeof(size_t) * 2;

   char* input_ptr = (char*)(input_map);

   input_ptr += offset;

   for (i = 0; i < diff; i += factor)
      append_block_len(r, *(size_t*)(input_ptr + i),
                       *(size_t*)(input_ptr + i + sizeof(size_t)));

   return r;
}