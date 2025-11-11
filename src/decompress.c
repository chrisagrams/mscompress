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

/**
 * @brief Creates a ZSTD decompression context and handles errors.
 *
 * @return A ZSTD decompression context on success. NULL on error.
 *
 */
ZSTD_DCtx* alloc_dctx()
{
   ZSTD_DCtx* dctx = ZSTD_createDCtx();
   if (dctx == NULL)
      error("alloc_dctx: ZSTD Context failed.\n");
   return dctx;
}

/**
* @brief Allocates a buffer for ZSTD decompression. Returns the buffer on success, NULL on error.
*/
void* alloc_ztsd_dbuff(size_t buff_len) {
   void* r = malloc(buff_len);
   if (r == NULL)
      error("alloc_ztsd_dbuff: malloc() error.\n");
   return r;
}


/**
 * @brief Decompresses a buffer using ZSTD returning the decompressed buffer on success, NULL on error.
 * @param dctx A ZSTD decompression context.
 * @param src_buff The compressed buffer to be decompressed.
 * @param src_len The length of the compressed buffer.
 * @param org_len The expected length of the decompressed buffer.
 * @return A pointer to the decompressed buffer on success. NULL on error.
*/
void* 
zstd_decompress(
   ZSTD_DCtx* dctx,
   void* src_buff,
   size_t src_len,
   size_t org_len
) {
   void* out_buff;
   size_t decmp_len = 0;

   out_buff = alloc_ztsd_dbuff(org_len);  // will return buff, NULL on error

   if (out_buff == NULL) {
      error("zstd_decompress: error in malloc()\n");
      return NULL;
   }

   decmp_len = ZSTD_decompressDCtx(dctx, out_buff, org_len, src_buff, src_len);

   if (decmp_len != org_len) {
      error(
         "zstd_decompress: ZSTD_decompressDCtx() error: %s\n",
         ZSTD_getErrorName(decmp_len)
      );
      free(out_buff);
      return NULL;
   }

   return out_buff;
}


/**
 * @brief Decompresses a buffer using LZ4 returning the decompressed buffer on success, NULL on error.
 * @param dctx A ZSTD decompression context (not used in this function, but included for consistency with other decompression functions).
 * @param src_buff The compressed buffer to be decompressed.
 * @param src_len The length of the compressed buffer.
 * @param org_len The expected length of the decompressed buffer.
 * @return A pointer to the decompressed buffer on success. NULL on error.
*/
void* lz4_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len,
                     size_t org_len) {
   void* out_buff;
   int decompressed_size;

   if (src_buff == NULL) {
      warning("lz4_decompress: src_buff is null.\n");
      return NULL;
   }
   if (src_len < 0) {
      warning("lz4_decompress: src_len < 0.\n");
      return NULL;
   }
   if (org_len < 0) {
      warning("lz4_decompress: org_len <0.\n");
      return NULL;
   }

   out_buff = malloc(org_len);
   if (out_buff == NULL) {
      warning("lz4_decompress: error in malloc()\n");
      return NULL;
   }

   decompressed_size =
       LZ4_decompress_safe(src_buff, out_buff, src_len, org_len);
   if (decompressed_size < 0) {
      warning("lz4_decompress: error in LZ4_decompress_safe\n");
      free(out_buff);
      return NULL;
   }

   return out_buff;
}


/**
 * @brief A no-op decompression function that simply copies the input buffer to the output buffer. Returns the output buffer on success, NULL on error.
 * @param dctx A ZSTD decompression context (not used in this function, but included for consistency with other decompression functions).
 * @param src_buff The compressed buffer to be "decompressed".
 * @param src_len The length of the compressed buffer.
 * @param org_len The expected length of the "decompressed" buffer.
 * @return A pointer to the "decompressed" buffer on success. NULL on error.
 */
void* no_decompress(ZSTD_DCtx* dctx, void* src_buff, size_t src_len,
                    size_t org_len)
{
   void* out_buff;
   size_t decmp_len = 0;

   out_buff = alloc_ztsd_dbuff(org_len);  // will return buff, NULL on error

   if (out_buff == NULL) {
      error("no_decompress: error in malloc()\n");
      return NULL;
   }

   memcpy(out_buff, src_buff, org_len);

   return out_buff;
}

/**
 * @brief Decompresses a block of data using the provided decompression function. Returns the decompressed buffer on success, NULL on error.
 * @param decompress_fun The decompression function to use.
 * @param dctx A ZSTD decompression context.
 * @param input_map The input buffer containing the compressed data.
 * @param offset The offset within the input buffer where the compressed data starts.
 * @param blk A block_len_t struct containing the original and compressed sizes of the data block
 * @return A pointer to the decompressed buffer on success. Can return NULL if the block is empty or if decompression fails.
 */
void* decmp_block(decompression_fun decompress_fun, ZSTD_DCtx* dctx,
                  void* input_map, long offset, block_len_t* blk) {
   if (blk == NULL)  // Empty block, return null.
      return NULL;

   void *out_buff = decompress_fun(
      dctx,
      (uint8_t*)input_map + offset,
      blk->compressed_size,
      blk->original_size
   );

   if (out_buff == NULL) {
      error("decmp_block: Decompression failed for block at offset %ld.\n", offset);
      return NULL;
   }

   return out_buff;
}


/**
 * @brief Allocates a decompress_args_t struct and initializes its fields. Returns the struct on success, NULL on error.
 * @param input_map The input buffer containing the compressed data.
 * @param df A pointer to a data_format_t struct containing the data format information.
 * @param xml_blk A pointer to a block_len_t struct containing the original and compressed sizes
 * @param mz_binary_blk A pointer to a block_len_t struct containing the original and compressed sizes of the m/z binary block.
 * @param inten_binary_blk A pointer to a block_len_t struct containing the original and compressed
 * @param division A pointer to a division_t struct containing the division information.
 * @param footer_xml_off The offset within the input buffer where the XML block starts.
 * @param footer_mz_bin_off The offset within the input buffer where the m/z binary block starts.
 * @param footer_inten_bin_off The offset within the input buffer where the intensity binary block starts.
 * @return A pointer to the allocated decompress_args_t struct on success. NULL on error.
 */
decompress_args_t* alloc_decompress_args(
   char* input_map,
   data_format_t* df,
   block_len_t* xml_blk,
   block_len_t* mz_binary_blk,
   block_len_t* inten_binary_blk,
   division_t* division,
   uint64_t footer_xml_off,
   uint64_t footer_mz_bin_off,
   uint64_t footer_inten_bin_off) 
   {
   decompress_args_t* r;

   r = malloc(sizeof(decompress_args_t));
   if (r == NULL) {
      error("alloc_decompress_args: malloc() error.\n");
      return NULL;
   }

   r->input_map = input_map;
   r->df = df;
   r->xml_blk = xml_blk;
   r->mz_binary_blk = mz_binary_blk;
   r->inten_binary_blk = inten_binary_blk;
   r->division = division;
   r->footer_xml_off = footer_xml_off;
   r->footer_mz_bin_off = footer_mz_bin_off;
   r->footer_inten_bin_off = footer_inten_bin_off;

   r->ret = NULL;
   r->ret_len = 0;

   return r;
}

/**
 * @brief Deallocates a decompress_args_t struct and its fields. Frees the memory allocated for the struct and its fields.
 * @param args A pointer to the decompress_args_t struct to be deallocated.
 */
void dealloc_decompress_args(decompress_args_t* args) {
   if (args) {
      if (args->ret)
         free(args->ret);
      free(args);
   }
}

/**
 * @brief Returns the index of the lowest value among three integers. Returns -1 if all values are equal.
 * @param i_0 The first integer.
 * @param i_1 The second integer.
 * @param i_2 The third integer.
 * @return The index of the lowest value among the three integers. Returns -1 if all values are equal.
 */
int get_lowest(int i_0, int i_1, int i_2) {
   int ret = -1;

   if (i_0 < i_1 && i_0 < i_2)
      ret = 0;
   else if (i_1 < i_0 && i_1 < i_2)
      ret = 1;
   else if (i_2 < i_0 && i_2 < i_1)
      ret = 2;

   return ret;
}

#ifdef _WIN32
/**
 * @brief Windows thread routine for decompression. Calls the decompress_routine function with the provided arguments.
 * @param lpParam A pointer to the decompress_args_t struct containing the arguments for decompression
 * @return 0 on success.
 */
DWORD WINAPI decompress_routine_win(LPVOID lpParam) {
   decompress_args_t* args = (decompress_args_t*)lpParam;
   decompress_routine(args);
   return 0;
}
#endif

/**
 * @brief Thread routine for decompression. Calls the decmp_block function to decompress the data blocks and writes the decompressed data to the output buffer.
 * @param args A pointer to the decompress_args_t struct containing the arguments for decompression.
 * @return Always returns NULL. `args->ret` will contain the decompressed data and `args->ret_len` will contain the length of the decompressed data on success.
 * on error, `args->ret` will be NULL and `args->ret_len` will be -1.
 *
 * Note: The caller is responsible for freeing the memory allocated for `args->ret`.
 */
void* decompress_routine(void* args) {
   // Get thread ID
   int tid = get_thread_id();

   // Cast the input argument to the correct type
   decompress_args_t* db_args = (decompress_args_t*)args;

   // Check if the input arguments are valid
   if (db_args == NULL) {
      error("decompress_routine: Decompression arguments are null.\n");
      return NULL;
   }

   // Initialize the return values to NULL and -1 in case of early return due to errors
   db_args->ret = NULL;
   db_args->ret_len = -1;

   // Allocate a decompression context
   ZSTD_DCtx* dctx = alloc_dctx();

   // Check if the decompression context was successfully allocated
   if (dctx == NULL) {
      error("decompress_routine: ZSTD Context failed.\n");
      return NULL;
   }

   // Get the division information from the arguments
   division_t* division = db_args->division;

   // Decompress each block of data
   char *decmp_xml = (char*)decmp_block(
            db_args->df->xml_decompression_fun, dctx, db_args->input_map,
            db_args->footer_xml_off, db_args->xml_blk),
        *decmp_mz_binary = (char*)decmp_block(
            db_args->df->mz_decompression_fun, dctx, db_args->input_map,
            db_args->footer_mz_bin_off, db_args->mz_binary_blk),
        *decmp_inten_binary = (char*)decmp_block(
            db_args->df->inten_decompression_fun, dctx, db_args->input_map,
            db_args->footer_inten_bin_off, db_args->inten_binary_blk);

   size_t binary_len = 0;

   int64_t buff_off = 0, xml_off = 0, mz_off = 0, inten_off = 0;
   int64_t xml_i = 0, mz_i = 0, inten_i = 0;

   int block = 0;

   long len = division->size;

   if (len <= 0) {
      error(
          "decompress_routine: Error determining decompression buffer size.\n");
      return NULL;
   }

   char* buff = malloc(len * 2);

   if (buff == NULL) {
      error(
          "decompress_routine: Failed to allocate buffer for decompression.\n");
      return NULL;
   }

   db_args->ret = buff;

   int64_t curr_len = 0;

   algo_args* a_args = malloc(sizeof(algo_args));
   
   if (a_args == NULL) {
      error("decompress_routine: Failed to allocate algo_args.\n");
      return NULL;
   }

   a_args->z = alloc_z_stream();

   if (a_args->z == NULL) {
      error("decompress_routine: Failed to allocate z_stream.\n");
      free(a_args);
      return NULL;
   }

   a_args->ret_code = 0; // Initialize return code to 0 (success).


   size_t algo_output_len = 0;
   a_args->dest_len = &algo_output_len;

   data_positions_t* curr_dp;

   while (block != -1) {
      switch (block) {
         case 0:  // xml
            curr_dp = division->xml;
            if (xml_i == curr_dp->total_spec) {
               block = -1;
               break;
            }
            curr_len =
                curr_dp->end_positions[xml_i] - curr_dp->start_positions[xml_i];
            if (curr_len == 0) {
               xml_i++;
               block++;
               break;
            }
            assert(curr_len > 0 && curr_len <= len);
            memcpy(buff + buff_off, decmp_xml + xml_off, curr_len);
            xml_off += curr_len;
            buff_off += curr_len;
            xml_i++;
            block++;
            break;
         case 1:  // mz
            curr_dp = division->mz;
            if (mz_i == curr_dp->total_spec) {
               block = 0;
               break;
            }
            curr_len =
                curr_dp->end_positions[mz_i] - curr_dp->start_positions[mz_i];
            if (curr_len == 0) {
               mz_i++;
               block++;
               break;
            }
            assert(curr_len > 0 && curr_len < len);
            a_args->src = (char**)&decmp_mz_binary;
            a_args->src_len = curr_len;
            a_args->dest = buff + buff_off;
            a_args->src_format = db_args->df->source_mz_fmt;
            a_args->enc_fun = db_args->df->encode_source_compression_mz_fun;
            a_args->scale_factor = db_args->df->mz_scale_factor;

            // Call the target mz function to encode the mz block and write it to the output buffer
            db_args->df->target_mz_fun((void*)a_args);

            if (a_args->ret_code != 0) {
               error("decompress_routine: Failed to encode mz block.\n");
               free(a_args);
               return NULL;
            }

            buff_off += *a_args->dest_len;
            mz_i++;
            block++;
            break;
         case 2:  // xml
            curr_dp = division->xml;
            if (xml_i == curr_dp->total_spec) {
               block = -1;
               break;
            }
            curr_len =
                curr_dp->end_positions[xml_i] - curr_dp->start_positions[xml_i];
            if (curr_len == 0) {
               xml_i++;
               block++;
               break;
            }
            assert(curr_len > 0 && curr_len < len);
            memcpy(buff + buff_off, decmp_xml + xml_off, curr_len);
            xml_off += curr_len;
            buff_off += curr_len;
            xml_i++;
            block++;
            break;
         case 3:  // int
            curr_dp = division->inten;
            if (inten_i == curr_dp->total_spec) {
               block = 0;
               break;
            }
            curr_len = curr_dp->end_positions[inten_i] -
                       curr_dp->start_positions[inten_i];
            if (curr_len == 0) {
               inten_i++;
               block = 0;
               break;
            }
            assert(curr_len > 0 && curr_len < len);
            a_args->src = (char**)&decmp_inten_binary;
            a_args->src_len = curr_len;
            a_args->dest = buff + buff_off;
            a_args->src_format = db_args->df->source_inten_fmt;
            a_args->enc_fun = db_args->df->encode_source_compression_inten_fun;
            a_args->scale_factor = db_args->df->int_scale_factor;

            // Call the target intensity function to encode the intensity block and write it to the output buffer
            db_args->df->target_inten_fun((void*)a_args);

            if (a_args->ret_code != 0) {
               error("decompress_routine: Failed to encode intensity block.\n");
               free(a_args);
               return NULL;
            }
            
            buff_off += *a_args->dest_len;
            inten_i++;
            block = 0;
            break;
         case -1:
            break;
      }
   }

   db_args->ret_len = buff_off;

   dealloc_z_stream(a_args->z);

   return NULL;
}


/**
 * @brief Decompresses an .msz file and writes the decompressed data to the provided file descriptor. Uses multiple threads to decompress the data in parallel.
 * @param input_map The input buffer containing the compressed data.
 * @param input_filesize The size of the input buffer.
 * @param arguments A pointer to an Arguments struct containing the command line arguments.
 * @param fd The file descriptor to write the decompressed data to.
 */
void decompress_msz(char* input_map, size_t input_filesize,
                    Arguments* arguments, int fd) {
   block_len_queue_t *xml_block_lens, *mz_binary_block_lens,
       *inten_binary_block_lens;
   footer_t* msz_footer;

   int n_divisions = 0;
   divisions_t* divisions;
   data_format_t* df;
   int threads = arguments->threads;

   print("\tDetected .msz file, reading header and footer...\n");

   df = get_header_df(input_map);

   parse_footer(&msz_footer, input_map, input_filesize, &xml_block_lens,
                &mz_binary_block_lens, &inten_binary_block_lens, &divisions,
                &n_divisions);

   if (n_divisions == 0) {
      warning("No divisions found in file, aborting...\n");
      return;
   }

   int ret = set_decompress_runtime_variables(df, msz_footer);
   if (ret != 0) {
      error("decompress_msz: Failed to set decompression runtime variables.\n");
      return;
   }

   decompress_args_t** args =
       malloc(sizeof(decompress_args_t*) * divisions->n_divisions);

#ifdef _WIN32
   HANDLE* ptid = (HANDLE*)malloc(sizeof(HANDLE) * divisions->n_divisions);
#else
   pthread_t* ptid =
       (pthread_t*)malloc(sizeof(pthread_t) * divisions->n_divisions);
#endif

   block_len_t *xml_blk, *mz_binary_blk, *inten_binary_blk;

   uint64_t footer_xml_off = 0, footer_mz_bin_off = 0,
            footer_inten_bin_off =
                0;  // offset within corresponding data_block.

   int i;

   int divisions_used = 0;
   int divisions_left = divisions->n_divisions;

   double start, stop;

   for (i = 0; i < divisions->n_divisions; i++) {
      xml_blk = pop_block_len(xml_block_lens);
      mz_binary_blk = pop_block_len(mz_binary_block_lens);
      inten_binary_blk = pop_block_len(inten_binary_block_lens);

      args[i] = alloc_decompress_args(
          input_map, df, xml_blk, mz_binary_blk, inten_binary_blk,
          divisions->divisions[i], footer_xml_off + msz_footer->xml_pos,
          footer_mz_bin_off + msz_footer->mz_binary_pos,
          footer_inten_bin_off + msz_footer->inten_binary_pos);

      if (xml_blk != NULL)
         footer_xml_off += xml_blk->compressed_size;
      if (mz_binary_blk != NULL)
         footer_mz_bin_off += mz_binary_blk->compressed_size;
      if (inten_binary_blk != NULL)
         footer_inten_bin_off += inten_binary_blk->compressed_size;
   }

   while (divisions_left > 0) {
      if (divisions_left < threads)
         threads = divisions_left;

      for (i = divisions_used; i < divisions_used + threads; i++) {
#ifdef _WIN32
         ptid[i] =
             CreateThread(NULL, 0, decompress_routine_win, args[i], 0, NULL);
         if (ptid[i] == NULL) {
            perror("CreateThread");
            return;
         }
#else
         int ret =
             pthread_create(&ptid[i], NULL, decompress_routine, (void*)args[i]);
         if (ret != 0) {
            perror("pthread_create");
            return;
         }
#endif
      }

#ifdef _WIN32
      WaitForMultipleObjects(threads, ptid + divisions_used, TRUE, INFINITE);
#else
      for (i = divisions_used; i < divisions_used + threads; i++) {
         int ret = pthread_join(ptid[i], NULL);
         if (ret != 0) {
            perror("pthread_join");
            return;
         }
      }
#endif

      for (i = divisions_used; i < divisions_used + threads; i++) {
         if (args[i]->ret == NULL || args[i]->ret_len == -1) {
            error("decompress_msz: Decompression failed for division %d.\n", i);
            return;
         }
         start = get_time();
         write_to_file(fd, args[i]->ret, args[i]->ret_len);
         stop = get_time();

         print("\tWrote %ld bytes to disk (%1.2fmb/s)\n", args[i]->ret_len,
               (float)args[i]->ret_len / (stop - start) / 1024 / 1024);

         dealloc_decompress_args(args[i]);
      }

      divisions_left -= threads;
      divisions_used += threads;
   }

   free(args);
   free(ptid);
}

/**
 * @brief Sets the decompression function based on the accession integer.
 * @param accession An integer representing the compression type.
 * @return A function pointer to the corresponding decompression function on success. NULL on error.
 */
decompression_fun set_decompress_fun(int accession) {
   switch (accession) {
      case _ZSTD_compression_:
         return zstd_decompress;
      case _LZ4_compression_:
         return lz4_decompress;
      case _no_comp_:
         return no_decompress;
      default:
         error("Compression type not supported.");
         return NULL;
   }
}