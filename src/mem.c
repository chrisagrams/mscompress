#include <stdlib.h>

#include "mscompress.h"

data_block_t* alloc_data_block(size_t max_size)
/**
 * @brief Allocates a data_block_t struct.
 *
 * @param max_size The maximum size the data block can store (cmp_blk_size)
 *
 * @return A populated data_block_t struct with capacity max_size and max_size
 * field set.
 */
{
#ifdef ERROR_CHECK
   if (max_size <= 0)
      error("alloc_data_block: invalid max_size for data block.\n");
#endif

   data_block_t* r = malloc(sizeof(data_block_t));

#ifdef ERROR_CHECK
   if (r == NULL)
      error("alloc_data_block: Failed to allocate data block.\n");
#endif

   r->mem = malloc(sizeof(char) * max_size);

#ifdef ERROR_CHECK
   if (r->mem == NULL)
      error("alloc_data_block: Failed to allocate data block memory.\n");
#endif

   r->size = 0;
   r->max_size = max_size;

   return r;
}

data_block_t* realloc_data_block(data_block_t* db, size_t new_size) {
#ifdef ERROR_CHECK
   if (db == NULL)
      error("realloc_data_block: db is NULL.\n");
   if (new_size <= 0)
      error("realloc_data_block: invalid new_size for data block.\n");
#endif

   db->mem = realloc(db->mem, new_size);

#ifdef ERROR_CHECK
   if (db->mem == NULL)
      error("realloc_data_block: Failed to reallocate data block memory.\n");
#endif

   db->max_size = new_size;

   return db;
}

void dealloc_data_block(data_block_t* db)
/**
 * @brief Deallocates a data_block_struct and its members.
 *
 * @param db An allocated data_block_struct.
 *
 */
{
   if (db) {
      if (db->mem)
         free(db->mem);
      free(db);
   }
   return;
}

cmp_block_t* alloc_cmp_block(char* mem, size_t size, size_t original_size)
/**
 * @brief Allocates and populates a cmp_block_t struct to store a compressed
 * block.
 *
 * @param mem Contents of compressed block.
 *
 * @param size Length of the compressed block.
 *
 * @returns A populated cmp_block_t struct with contents of compressed block.
 *
 */
{
#ifdef ERROR_CHECK
   if (mem == NULL)
      error("alloc_cmp_block: invalid mem for cmp block.\n");

   if (size <= 0 || original_size <= 0)
      error("alloc_cmp_block: invalid size for cmp block.\n");
#endif

   cmp_block_t* r = malloc(sizeof(cmp_block_t));

#ifdef ERROR_CHECK
   if (r == NULL)
      error("alloc_cmp_block: Failed to allocate cmp block.\n");
#endif

   r->mem = mem;
   r->size = size;
   r->max_size = size;
   r->original_size = original_size;
   return r;
}

void dealloc_cmp_block(cmp_block_t* blk) {
   if (blk) {
      if (blk->mem)
         free(blk->mem);
      else
         error("dealloc_cmp_block: blk's mem is NULL\n");
      free(blk);
   } else
      error("dealloc_cmp_block: NULL pointer passed to dealloc_cmp_block.\n");
   return;
}
