#include <stdlib.h>

#include "mscompress.h"

/**
 * @brief Allocates a data_block_t struct with a specified maximum size.
 * @param max_size The maximum size of the data block.
 * @return A pointer to the allocated data_block_t struct on success. NULL on error.
 */
data_block_t* alloc_data_block(size_t max_size)
{
   if (max_size <= 0) {
      error("alloc_data_block: invalid max_size for data block.\n");
      return NULL;
   }

   data_block_t* r = malloc(sizeof(data_block_t));

   if (r == NULL) {
      error("alloc_data_block: Failed to allocate data block.\n");
      return NULL;
   }

   r->mem = malloc(sizeof(char) * max_size);

   if (r->mem == NULL) {
      error("alloc_data_block: Failed to allocate data block memory.\n");
      free(r);
      return NULL;
   }

   r->size = 0;
   r->max_size = max_size;

   return r;
}

/**
 * @brief Reallocates a `data_block_t` struct to a new size.
 * @param db A pointer to the `data_block_t` struct to be reallocated.
 * @param new_size The new size for the data block.
 * @return A pointer to the reallocated data_block_t struct on success. NULL on error.
 */
data_block_t* realloc_data_block(data_block_t* db, size_t new_size) {
   if (db == NULL) {
      error("realloc_data_block: db is NULL.\n");
      return NULL;
   }
   if (new_size <= 0) {
      error("realloc_data_block: invalid new_size for data block.\n");
      return NULL;
   }

   if (new_size <= db->max_size) {
      // No need to reallocate if the new size is less than or equal to the current max size
      return db;
   }

   db->mem = realloc(db->mem, new_size);

   if (db->mem == NULL) {
      error("realloc_data_block: Failed to reallocate data block memory.\n");
      return NULL;
   }

   db->max_size = new_size;

   return db;
}

/**
 * @brief Deallocates a `data_block_t` struct and its memory.
 * @param db A pointer to the `data_block_t` struct to be deallocated.
 * @return 0 on success, -1 on error.
 */
int dealloc_data_block(data_block_t* db)
{
   if (db) {
      if (db->mem)
         free(db->mem);
      else {
         error("dealloc_data_block: db's mem is NULL\n");
         return -1;
      }
      free(db);
   }
   else {
      error("dealloc_data_block: NULL pointer passed to dealloc_data_block.\n");
      return -1;
   }
   return 0;
}


/**
 * @brief Allocates and populates a `cmp_block_t` struct to store a compressed
 * block.
 * @param mem Contents of compressed block.
 * @param size Length of the compressed block.
 * @param original_size The original size of the data before compression.
 * @return A populated `cmp_block_t` struct with contents of compressed block on success. NULL on error.
 */
cmp_block_t* alloc_cmp_block(char* mem, size_t size, size_t original_size)
{
   cmp_block_t* r = malloc(sizeof(cmp_block_t));

   if (r == NULL) {
      error("alloc_cmp_block: Failed to allocate cmp block.\n");
      return NULL;
   }

   r->mem = mem;
   r->size = size;
   r->max_size = size;
   r->original_size = original_size;
   return r;
}

/**
 * @brief Deallocates a `cmp_block_t` struct and its memory.
 * @param blk A pointer to the `cmp_block_t` struct to be deallocated.
 * @returns 0 on success, -1 on error.
 */
int dealloc_cmp_block(cmp_block_t* blk) {
   if (blk) {
      if (blk->mem)
         free(blk->mem);
      free(blk);
   } else {
      error("dealloc_cmp_block: NULL pointer passed to dealloc_cmp_block.\n");
      return -1;
   }
   return 0;
}
