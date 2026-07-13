#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION ms_mutex_t;
#define MS_MUTEX_INIT(m) InitializeCriticalSection(m)
#define MS_MUTEX_DESTROY(m) DeleteCriticalSection(m)
#define MS_MUTEX_LOCK(m) EnterCriticalSection(m)
#define MS_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#else
#include <pthread.h>
typedef pthread_mutex_t ms_mutex_t;
#define MS_MUTEX_INIT(m) pthread_mutex_init((m), NULL)
#define MS_MUTEX_DESTROY(m) pthread_mutex_destroy(m)
#define MS_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define MS_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#endif

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
   r->cache_spec_lens = NULL;

   r->lru_prev = NULL;
   r->lru_next = NULL;
   r->lru_pinned = 0;

   return r;
}

/**
 * @brief Deallocates a `block_len_t` node and all of its owned memory.
 *
 * Frees the cache, `encoded_cache`, `encoded_cache_lens`, and `cache_spec_lens`
 * fields if they are non-NULL, then frees the `block_len_t` struct itself. Safe
 * to call with NULL.
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
      if (blk->cache_spec_lens) {
         free(blk->cache_spec_lens);
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
 * @return Total number of bytes written to the file descriptor.
 *
 * @warning This function consumes and frees the queue. The pointer is invalid after return.
 */
size_t dump_block_len_queue(block_len_queue_t* queue, int fd) {
   block_len_t* curr;
   block_len_t* prev;
   char buff[sizeof(size_t)];
   size_t total_written = 0;

   size_t* buff_cast = (size_t*)(&buff[0]);

   curr = queue->head;

   while (curr != NULL) {
      *buff_cast = curr->original_size;
      total_written += write_to_file(fd, buff, sizeof(size_t));

      *buff_cast = curr->compressed_size;
      total_written += write_to_file(fd, buff, sizeof(size_t));

      prev = curr;
      curr = curr->next;
      dealloc_block_len(prev);
   }

   free(queue);

   return total_written;
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
/* ======================================================================
 * Shared, byte-budgeted block-cache LRU.
 *
 * One block_lru_t can hold blocks from many files' queues at once — the
 * block_len_t nodes don't know which file they belong to, so a single shared
 * instance bounds aggregate decompressed-cache memory across all open files.
 * ====================================================================== */

/**
 * @brief Frees a block's decompressed-block caches without deallocating the node.
 *
 * Frees `cache`, `encoded_cache`, `encoded_cache_lens`, and `cache_spec_lens`
 * if non-NULL and resets them to NULL/zero. Used by the LRU eviction path and
 * by `clear_cache`-style APIs. Does not touch structural fields (sizes, `next`)
 * or LRU links.
 */
void block_drop_cache(block_len_t* blk) {
   if (!blk)
      return;
   if (blk->cache) {
      free(blk->cache);
      blk->cache = NULL;
   }
   if (blk->encoded_cache) {
      free(blk->encoded_cache);
      blk->encoded_cache = NULL;
   }
   if (blk->encoded_cache_lens) {
      free(blk->encoded_cache_lens);
      blk->encoded_cache_lens = NULL;
   }
   if (blk->cache_spec_lens) {
      free(blk->cache_spec_lens);
      blk->cache_spec_lens = NULL;
   }
   blk->encoded_cache_fmt = 0;
   blk->encoded_cache_len = 0;
}

/**
 * @brief Allocates a shared, byte-budgeted block-cache LRU.
 *
 * @param cap_bytes Maximum total decompressed-cache bytes held at once. 0 means
 *                  "no limit" (callers should pass NULL instead of allocating
 *                  an unbounded LRU at all).
 * @warning Terminates the program if allocation fails.
 */
block_lru_t* alloc_block_lru(size_t cap_bytes) {
   block_lru_t* lru = malloc(sizeof(block_lru_t));
   if (!lru) {
      fprintf(stderr, "alloc_block_lru: malloc failed\n");
      exit(-1);
   }
   lru->head = NULL;
   lru->tail = NULL;
   lru->cur_bytes = 0;
   lru->cap_bytes = cap_bytes;

   ms_mutex_t* lock = malloc(sizeof(ms_mutex_t));
   if (!lock) {
      fprintf(stderr, "alloc_block_lru: mutex malloc failed\n");
      exit(-1);
   }
   MS_MUTEX_INIT(lock);
   lru->lock = (void*)lock;
   return lru;
}

/**
 * @brief Frees the LRU struct and its mutex.
 *
 * Does NOT touch the `block_len_t` nodes referenced by the LRU — those remain
 * owned by their containing queues. Callers that share an LRU across files must
 * `lru_remove_queue_blocks()` a file's blocks before that file's queues are
 * freed; see the close path. If you want the cached buffers freed before the
 * LRU goes away, call `lru_evict_all()` first.
 */
void dealloc_block_lru(block_lru_t* lru) {
   if (!lru)
      return;
   if (lru->lock) {
      ms_mutex_t* lock = (ms_mutex_t*)lru->lock;
      MS_MUTEX_DESTROY(lock);
      free(lock);
   }
   free(lru);
}

/* Internal: detach `blk` from `lru`'s doubly-linked list. Caller holds the
 * lock and updates `lru_pinned` / `cur_bytes`. */
static void lru_unlink(block_lru_t* lru, block_len_t* blk) {
   if (blk->lru_prev)
      blk->lru_prev->lru_next = blk->lru_next;
   else
      lru->head = blk->lru_next;
   if (blk->lru_next)
      blk->lru_next->lru_prev = blk->lru_prev;
   else
      lru->tail = blk->lru_prev;
   blk->lru_prev = NULL;
   blk->lru_next = NULL;
}

/* Internal: evict tail blocks until within the byte budget. Caller holds lock.
 *
 * NEVER evicts the head (most-recently-touched) block: the caller touches a
 * block immediately before reading its `cache`, so evicting the just-touched
 * block here would free the buffer out from under that read (use-after-free).
 * Consequently the budget is a SOFT cap — peak usage can exceed cap_bytes by up
 * to one block (whenever a single block is larger than the whole budget). */
static void lru_evict_to_budget(block_lru_t* lru) {
   while (lru->cap_bytes > 0 && lru->cur_bytes > lru->cap_bytes && lru->tail &&
          lru->tail != lru->head) {
      block_len_t* victim = lru->tail;
      lru_unlink(lru, victim);
      victim->lru_pinned = 0;
      if (lru->cur_bytes >= victim->original_size)
         lru->cur_bytes -= victim->original_size;
      else
         lru->cur_bytes = 0;
      block_drop_cache(victim);
   }
}

/**
 * @brief Touch a block in the shared LRU. Inserts on first touch (accounting
 *        `blk->original_size` bytes), bumps to head on subsequent touches, and
 *        evicts least-recently-used tail blocks until within the byte budget.
 *
 * Eviction frees the evicted block's cache buffers via `block_drop_cache()`.
 * Pass NULL for `lru` to disable bounding entirely (legacy unbounded caching).
 * Must be called AFTER `blk->cache` is populated so the accounted bytes match
 * what an eviction would free.
 */
void lru_touch_block(block_lru_t* lru, block_len_t* blk) {
   if (!lru || !blk)
      return;
   if (lru->cap_bytes == 0)
      return;  // unbounded — nothing to bound

   ms_mutex_t* lock = (ms_mutex_t*)lru->lock;
   MS_MUTEX_LOCK(lock);

   if (blk->lru_pinned) {
      // Already cached: move to head if not already there.
      if (lru->head != blk) {
         lru_unlink(lru, blk);
         blk->lru_prev = NULL;
         blk->lru_next = lru->head;
         if (lru->head)
            lru->head->lru_prev = blk;
         lru->head = blk;
         if (!lru->tail)
            lru->tail = blk;
      }
      MS_MUTEX_UNLOCK(lock);
      return;
   }

   // First touch: insert at head and account its bytes.
   blk->lru_pinned = 1;
   lru->cur_bytes += blk->original_size;
   blk->lru_prev = NULL;
   blk->lru_next = lru->head;
   if (lru->head)
      lru->head->lru_prev = blk;
   lru->head = blk;
   if (!lru->tail)
      lru->tail = blk;

   lru_evict_to_budget(lru);
   MS_MUTEX_UNLOCK(lock);
}

/**
 * @brief Remove every block belonging to `queue` from the shared LRU, freeing
 *        their cache buffers and subtracting their bytes.
 *
 * MUST be called for each of a file's block queues before those queues are
 * deallocated, so the shared LRU never retains dangling pointers into a freed
 * file. Walks the queue (O(blocks in file)) and unlinks any pinned node.
 */
void lru_remove_queue_blocks(block_lru_t* lru, block_len_queue_t* queue) {
   if (!lru || !queue)
      return;
   ms_mutex_t* lock = (ms_mutex_t*)lru->lock;
   MS_MUTEX_LOCK(lock);
   block_len_t* curr = queue->head;
   while (curr) {
      if (curr->lru_pinned) {
         lru_unlink(lru, curr);
         curr->lru_pinned = 0;
         if (lru->cur_bytes >= curr->original_size)
            lru->cur_bytes -= curr->original_size;
         else
            lru->cur_bytes = 0;
         block_drop_cache(curr);
      }
      curr = curr->next;
   }
   MS_MUTEX_UNLOCK(lock);
}

/**
 * @brief Evict every block currently held by the LRU, freeing their cache
 *        buffers. The LRU struct itself is left allocated and empty.
 */
void lru_evict_all(block_lru_t* lru) {
   if (!lru)
      return;
   ms_mutex_t* lock = (ms_mutex_t*)lru->lock;
   MS_MUTEX_LOCK(lock);
   block_len_t* curr = lru->head;
   while (curr) {
      block_len_t* nx = curr->lru_next;
      curr->lru_prev = NULL;
      curr->lru_next = NULL;
      curr->lru_pinned = 0;
      block_drop_cache(curr);
      curr = nx;
   }
   lru->head = NULL;
   lru->tail = NULL;
   lru->cur_bytes = 0;
   MS_MUTEX_UNLOCK(lock);
}

/** @brief Current total cached bytes held by the LRU (0 if NULL). */
size_t block_cache_usage(block_lru_t* lru) {
   if (!lru)
      return 0;
   ms_mutex_t* lock = (ms_mutex_t*)lru->lock;
   MS_MUTEX_LOCK(lock);
   size_t v = lru->cur_bytes;
   MS_MUTEX_UNLOCK(lock);
   return v;
}

/** @brief Configured byte budget of the LRU (0 if NULL / unbounded). */
size_t block_cache_cap(block_lru_t* lru) {
   if (!lru)
      return 0;
   return lru->cap_bytes;
}
