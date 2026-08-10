#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"
#include "algos/algos.h"

/**
 * @brief Updates the spectrumList count value in an mzML header.
 * @param header The header buffer to modify.
 * @param header_len Current header length.
 * @param new_count The new spectrum count to set.
 * @param out_len Pointer to store the updated header length.
 * @return New header buffer with updated count, or `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 */
char* update_spectrum_list_count(char* header, size_t header_len,
                                 long new_count, size_t* out_len) {
   if (!header || header_len == 0) {
      return NULL;
   }

   // Find spectrumList tag
   char* spectrum_list = strstr(header, "spectrumList");
   if (!spectrum_list || (spectrum_list - header) >= (long)header_len) {
      // No spectrumList found, return copy of original
      char* result = malloc(header_len);
      if (result) {
         memcpy(result, header, header_len);
         *out_len = header_len;
      }
      return result;
   }

   // Find count=" within spectrumList tag (before the closing >)
   char* tag_end = strstr(spectrum_list, ">");
   if (!tag_end) {
      char* result = malloc(header_len);
      if (result) {
         memcpy(result, header, header_len);
         *out_len = header_len;
      }
      return result;
   }

   char* count_attr = strstr(spectrum_list, "count=\"");
   if (!count_attr || count_attr > tag_end) {
      // count attribute not found in spectrumList tag
      char* result = malloc(header_len);
      if (result) {
         memcpy(result, header, header_len);
         *out_len = header_len;
      }
      return result;
   }

   // Move to the start of the count value
   char* count_value_start = count_attr + 7;  // strlen("count=\"") = 7
   char* count_value_end = strstr(count_value_start, "\"");
   if (!count_value_end) {
      char* result = malloc(header_len);
      if (result) {
         memcpy(result, header, header_len);
         *out_len = header_len;
      }
      return result;
   }

   // Calculate old value length
   size_t old_value_len = count_value_end - count_value_start;

   // Convert new count to string
   char new_count_str[32];
   int new_value_len =
       snprintf(new_count_str, sizeof(new_count_str), "%ld", new_count);

   // Calculate new header length
   size_t prefix_len = count_value_start - header;
   size_t suffix_len = header_len - (count_value_end - header);
   size_t new_header_len = prefix_len + new_value_len + suffix_len;

   // Allocate new buffer
   char* result = malloc(new_header_len);
   if (!result) {
      return NULL;
   }

   // Copy prefix (everything before the old count value)
   memcpy(result, header, prefix_len);

   // Copy new count value
   memcpy(result + prefix_len, new_count_str, new_value_len);

   // Copy suffix (everything after the old count value)
   memcpy(result + prefix_len + new_value_len, count_value_end, suffix_len);

   *out_len = new_header_len;
   return result;
}

/**
 * @brief Reconstructs and writes a complete mzML file from its divided streams.
 * @param input_map The memory-mapped input buffer containing the original data.
 * @param divisions The divisions containing XML, m/z, and intensity position mappings.
 * @param output_fd The file descriptor to write the reconstructed mzML output to.
 */
void extract_mzml(char* input_map, divisions_t* divisions, int output_fd) {
   for (int i = 0; i < divisions->n_divisions; i++) {
      division_t* division = divisions->divisions[i];

      long total_spec = division->mz->total_spec;

      long j = 0;

      char* buff = malloc(division->size);

      long len = 0;

      long out_len = 0;

      int64_t xml_i = 0, mz_i = 0, inten_i = 0;

      while (j <= total_spec) {
         // XML
         len = division->xml->end_positions[xml_i] -
               division->xml->start_positions[xml_i];
         memcpy(buff + out_len,
                input_map + division->xml->start_positions[xml_i], len);
         out_len += len;
         xml_i++;

         // mz
         len = division->mz->end_positions[mz_i] -
               division->mz->start_positions[mz_i];
         memcpy(buff + out_len, input_map + division->mz->start_positions[mz_i],
                len);
         out_len += len;
         mz_i++;

         // XML
         len = division->xml->end_positions[xml_i] -
               division->xml->start_positions[xml_i];
         memcpy(buff + out_len,
                input_map + division->xml->start_positions[xml_i], len);
         out_len += len;
         xml_i++;

         // inten
         len = division->inten->end_positions[inten_i] -
               division->inten->start_positions[inten_i];
         memcpy(buff + out_len,
                input_map + division->inten->start_positions[inten_i], len);
         out_len += len;
         inten_i++;

         j++;
      }

      // end case
      len = division->xml->end_positions[xml_i] -
            division->xml->start_positions[xml_i];
      memcpy(buff + out_len, input_map + division->xml->start_positions[xml_i],
             len);
      out_len += len;
      xml_i++;

      write_to_file(output_fd, buff, out_len);
      free(buff);
   }
   return;
}

/**
 * @brief Determines which division contains a given spectrum index.
 * @param divisions The divisions structure to search.
 * @param index The global spectrum index to locate.
 * @return The zero-based division index containing the spectrum, or -1 if not found.
 */
int determine_division_by_index(divisions_t* divisions, long index) {
   int i = 0;
   long sum = 0;

   for (i; i < divisions->n_divisions; i++) {
      division_t* curr = divisions->divisions[i];
      if (curr) {
         if (curr->mz) {
            sum += curr->mz->total_spec;
            if (index < sum) {
               return i;
            }
         }
      }
   }
   return -1;
}

/**
 * @brief Resolves the byte start and end positions of a spectrum by its global index.
 * @param divisions The divisions structure containing spectra position data.
 * @param index The global spectrum index to look up.
 * @param start Pointer to store the resolved start byte position.
 * @param end Pointer to store the resolved end byte position.
 */
void determine_spectrum_start_end(divisions_t* divisions, long index,
                                  uint64_t* start, uint64_t* end) {
   division_t* curr;
   data_positions_t* spectra;
   int division_index = 0;
   long offset = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      curr = divisions->divisions[i];
      spectra = curr->spectra;

      if (index < spectra->total_spec + offset)
         break;

      offset += spectra->total_spec;
      division_index++;
   }

   *start = divisions->divisions[division_index]
                ->spectra->start_positions[index - offset];
   *end = divisions->divisions[division_index]
              ->spectra->end_positions[index - offset];
   return;
}

/**
 * @brief Determines which division contains a given target spectrum offset.
 * @param divisions The divisions structure to search.
 * @param target The target spectrum offset (cumulative count) to locate.
 * @return The zero-based division index containing the target, or 0 if not found.
 * @note Returns -1 if a division or its spectra pointer is `NULL`.
 */
int determine_division(divisions_t* divisions, long target) {
   int i = 0;
   long sum = 0;
   for (i; i < divisions->n_divisions; i++) {
      division_t* curr = divisions->divisions[i];

      if (!curr) {
         warning("determine_division: curr division is NULL.\n");
         return -1;
      }
      data_positions_t* spectra = curr->spectra;

      if (!spectra) {
         warning("determine_division: spectra is NULL.\n");
         // return -1;
      } else {
         sum += spectra->total_spec;
         if (target < sum)
            return i;
      }
   }
   return 0;
}

/**
 * @brief Extracts the mzML header from the first division. (From byte 0 ->
 * first spectra start)
 * @param blk The input buffer containing the data.
 * @param first_divison The first division containing the header information.
 * @param out_len Pointer to a `size_t` where the length of the extracted header
 * will be stored.
 * @return A pointer to the extracted mzML header on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 */
char* extract_mzml_header(char* blk, division_t* first_division,
                          size_t* out_len) {
   data_positions_t *spectra, *xml;
   char* res;

   if (!first_division)
      return NULL;

   spectra = first_division->spectra;
   xml = first_division->xml;

   if (xml->start_positions[0] != 0)  // First division must start at 0 (header)
      return NULL;

   *out_len = spectra->start_positions[0] - xml->start_positions[0];

   res = malloc(*out_len);

   memcpy(res, blk, *out_len);

   return res;
}

/**
 * @brief Extracts the mzML footer from the last division. (From last spectra
 * end -> last XML end)
 * @param blk The input buffer containing the data.
 * @param divisions The divisions containing the footer information.
 * @param out_len Pointer to a `size_t` where the length of the extracted footer
 * will be stored.
 * @return A pointer to the extracted mzML footer on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 */
char* extract_mzml_footer(char* blk, divisions_t* divisions, size_t* out_len) {
   data_positions_t *spectra, *xml;
   char* res;

   uint64_t last_spectra_end = 0;
   int last_spectra_division = 0;  // Index of division

   uint64_t last_xml_end = 0;
   int last_xml_division = 0;  // Index of division

   // Determine last spectra end position
   for (int i = 0; i < divisions->n_divisions; i++) {
      division_t* curr = divisions->divisions[i];
      spectra = curr->spectra;

      uint64_t last = spectra->end_positions[spectra->total_spec - 1];
      if (last > last_spectra_end) {
         last_spectra_end = last;
         last_spectra_division = i;
      }
   }

   // Determine last XML end position
   for (int i = 0; i < divisions->n_divisions; i++) {
      division_t* curr = divisions->divisions[i];
      xml = curr->xml;

      uint64_t last = xml->end_positions[xml->total_spec - 1];
      if (last > last_xml_end) {
         last_xml_end = last;
         last_xml_division = i;
      }
   }

   size_t offset =
       last_spectra_end -
       divisions->divisions[last_xml_division]->xml->start_positions[0];

   *out_len = last_xml_end - last_spectra_end;

   res = malloc(*out_len);

   memcpy(res, blk + offset, *out_len);

   return res;
}

/**
 * @brief Extracts the XML block corresponding to the start of a spectrum from
 * the input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param xml_block_lens A pointer to a `block_len_queue_t` struct containing
 * the lengths of the XML blocks.
 * @param xml_pos The offset within the input buffer where the XML blocks start.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param spectrum_start The starting position of the spectrum to extract.
 * @param spectrum_end The ending position of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted XML
 * block will be stored.
 * @return A pointer to the extracted XML block on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 * @note Decompressed data is cached in the `block_len_t` node to avoid redundant decompression.
 */
char* extract_spectrum_start_xml(char* input_map, ZSTD_DCtx* dctx,
                                 data_format_t* df,
                                 block_len_queue_t* xml_block_lens,
                                 long xml_pos, divisions_t* divisions,
                                 uint64_t spectrum_start, uint64_t spectrum_end,
                                 size_t* out_len, block_lru_t* lru) {
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index = -1;
   long xml_start_offset;  // Offset relative to spectrum
   long xml_buff_offset;   // Offset relative to start of compressed block
   long xml_sum;
   block_len_t* xml_blk_len;
   long xml_blk_offset;

   int found = 0;

   // Find XML start position
   for (int i = 0; i < divisions->n_divisions; i++) {
      if (found)
         break;
      division_t* curr = divisions->divisions[i];
      xml = curr->xml;

      xml_sum = 0;
      for (int j = 0; j < xml->total_spec * 2; j++) {
         if (spectrum_start > xml->start_positions[j] &&
             spectrum_start < xml->end_positions[j]) {
            xml_start_position = xml->start_positions[j];
            xml_end_position = xml->end_positions[j];
            division_index = i;
            xml_start_offset = spectrum_start - xml_start_position;
            xml_buff_offset = xml_sum;
            found = 1;
            break;
         }
         xml_sum += xml->end_positions[j] - xml->start_positions[j];
      }
   }

   if (!found) {
      error("extract_spectrum_start_xml: Could not find division for spectrum.\n");
      return NULL;
   }

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      if (decmp_xml == NULL) {
         error("extract_spectrum_start_xml: Failed to decompress XML block.\n");
         return NULL;
      }
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;
   lru_touch_block(lru, xml_blk_len);

   *out_len = xml_end_position - xml_start_position - xml_start_offset;
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset + xml_start_offset, *out_len);

   return res;
}

/**
 * @brief Extracts the XML block corresponding to the end of a spectrum from the
 * input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param xml_block_lens A pointer to a `block_len_queue_t` struct containing
 * the lengths of the XML blocks.
 * @param xml_pos The offset within the input buffer where the XML blocks start.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param spectrum_start The starting position of the spectrum to extract.
 * @param spectrum_end The ending position of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted XML
 * block will be stored.
 * @return A pointer to the extracted XML block on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 * @note Decompressed data is cached in the `block_len_t` node to avoid redundant decompression.
 */
char* extract_spectrum_inner_xml(char* input_map, ZSTD_DCtx* dctx,
                                 data_format_t* df,
                                 block_len_queue_t* xml_block_lens,
                                 long xml_pos, divisions_t* divisions,
                                 uint64_t spectrum_start, uint64_t spectrum_end,
                                 size_t* out_len, block_lru_t* lru) {
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index = -1;
   long xml_buff_offset;  // Offset relative to start of compressed block
   long xml_sum;
   block_len_t* xml_blk_len;
   long xml_blk_offset;

   int found = 0;

   // Find XML start position
   for (int i = 0; i < divisions->n_divisions; i++) {
      if (found)
         break;
      division_t* curr = divisions->divisions[i];
      xml = curr->xml;

      xml_sum = 0;
      for (int j = 0; j < xml->total_spec * 2; j++) {
         if (xml->start_positions[j] > spectrum_start &&
             xml->start_positions[j] < spectrum_end &&
             xml->end_positions[j] > spectrum_start &&
             xml->end_positions[j] < spectrum_end) {
            xml_start_position = xml->start_positions[j];
            xml_end_position = xml->end_positions[j];
            division_index = i;
            xml_buff_offset = xml_sum;
            found = 1;
            break;
         }
         xml_sum += xml->end_positions[j] - xml->start_positions[j];
      }
   }

   if (!found) {
      error("extract_spectrum_inner_xml: Could not find division for spectrum.\n");
      return NULL;
   }

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      if (decmp_xml == NULL) {
         error("extract_spectrum_inner_xml: Failed to decompress XML block.\n");
         return NULL;
      }
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;
   lru_touch_block(lru, xml_blk_len);

   *out_len = xml_end_position - xml_start_position;
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset, *out_len);

   return res;
}

/**
 * @brief Extracts the XML block corresponding to the end of a spectrum from the
 * input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param xml_block_lens A pointer to a `block_len_queue_t` struct containing
 * the lengths of the XML blocks.
 * @param xml_pos The offset within the input buffer where the XML blocks start.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param spectrum_start The starting position of the spectrum to extract.
 * @param spectrum_end The ending position of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted XML
 * block will be stored.
 * @return A pointer to the extracted XML block on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 * @note Decompressed data is cached in the `block_len_t` node to avoid redundant decompression.
 */
char* extract_spectrum_last_xml(char* input_map, ZSTD_DCtx* dctx,
                                data_format_t* df,
                                block_len_queue_t* xml_block_lens, long xml_pos,
                                divisions_t* divisions, uint64_t spectrum_start,
                                uint64_t spectrum_end, size_t* out_len,
                                block_lru_t* lru) {
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index = -1;
   long xml_buff_offset;  // Offset relative to start of compressed block
   long xml_sum;
   block_len_t* xml_blk_len;
   long xml_blk_offset;

   int found = 0;

   // Find XML start position
   for (int i = 0; i < divisions->n_divisions; i++) {
      if (found)
         break;
      division_t* curr = divisions->divisions[i];
      xml = curr->xml;

      xml_sum = 0;
      for (int j = 0; j < xml->total_spec * 2; j++) {
         if (xml->start_positions[j] > spectrum_start &&
             xml->start_positions[j] < spectrum_end &&
             xml->end_positions[j] > spectrum_start &&
             xml->end_positions[j] > spectrum_end) {
            xml_start_position = xml->start_positions[j];
            xml_end_position = xml->end_positions[j];
            division_index = i;
            xml_buff_offset = xml_sum;
            found = 1;
            break;
         }
         xml_sum += xml->end_positions[j] - xml->start_positions[j];
      }
   }

   if (!found) {
      error("extract_spectrum_last_xml: Could not find division for spectrum.\n");
      return NULL;
   }

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      if (decmp_xml == NULL) {
         error("extract_spectrum_last_xml: Failed to decompress XML block.\n");
         return NULL;
      }
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;
   lru_touch_block(lru, xml_blk_len);

   *out_len = (xml_end_position - xml_start_position) -
              (xml_end_position - spectrum_end) + 1;  //+1 For newline
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset, *out_len);

   return res;
}

/**
 * @brief Fast-path extraction of a single spectrum's payload from a cached
 *        decompressed block, bypassing `encode_binary_block`.
 *
 * The decompressed block layout is per-spectrum `{ZLIB_TYPE header}{payload}`
 * (see `no_encode_w_header`). When the caller doesn't need the block re-encoded
 * (Python lossless path), we can read the index'th payload directly out of
 * `blk->cache` — saving the full-block allocation + per-spectrum walk that
 * `encode_binary_block` performs.
 *
 * O(index) over the per-spectrum headers. For typical block sizes the walk is
 * dominated by L1 cache hits; the saved full-block malloc + memcpy round-trip
 * is the real win.
 *
 * @return Newly-malloc'd buffer of size `*out_len` (caller frees). NULL on error.
 */
static char* extract_no_encode_from_cache(block_len_t* blk,
                                          data_positions_t* dp, long index,
                                          size_t* out_len) {
   if (!blk || !blk->cache || !dp) {
      error("extract_no_encode_from_cache: NULL block/cache/dp");
      return NULL;
   }
   if (index < 0 || (uint64_t)index >= dp->total_spec) {
      error("extract_no_encode_from_cache: index %ld out of range [0, %lu)",
            index, (unsigned long)dp->total_spec);
      return NULL;
   }

   // Lazy-populate cache_spec_lens with the per-spectrum payload sizes parsed
   // out of the cache headers. Once populated, subsequent calls only walk this
   // small contiguous array to compute offsets. Empty spectra (skipped by the
   // compressor — see compress.c "if (len == 0) continue") get lens[i]=0 and
   // contribute no bytes to the cache.
   //
   // Note: distinct from `encoded_cache_lens`, which the slow-path
   // (encode_binary_block) uses for *re-encoded* sizes — reusing that field
   // here would conflate two different semantics when both paths touch the
   // same block (e.g. spectrum.xml then spectrum.mz on the same MSZFile).
   if (!blk->cache_spec_lens) {
      size_t* lens = malloc(dp->total_spec * sizeof(size_t));
      if (!lens) {
         error("extract_no_encode_from_cache: malloc(lens, %lu) failed",
               (unsigned long)(dp->total_spec * sizeof(size_t)));
         return NULL;
      }
      const char* cursor = blk->cache;
      for (uint64_t i = 0; i < dp->total_spec; i++) {
         if (dp->end_positions[i] == dp->start_positions[i]) {
            lens[i] = 0;
            continue;
         }
         ZLIB_TYPE plen = *(const ZLIB_TYPE*)cursor;
         lens[i] = (size_t)plen;
         cursor += ZLIB_SIZE_OFFSET + plen;
      }
      blk->cache_spec_lens = lens;
   }

   // Compute byte offset of the target spectrum's payload within the cache.
   size_t offset = 0;
   for (long i = 0; i < index; i++) {
      size_t L = blk->cache_spec_lens[i];
      if (L > 0)
         offset += ZLIB_SIZE_OFFSET + L;
   }

   size_t len = blk->cache_spec_lens[index];
   *out_len = len;
   // Empty spectrum: callers free(res) unconditionally; return a freeable
   // non-NULL (matching extract_from_encoded_block convention).
   if (len == 0)
      return malloc(1);

   offset += ZLIB_SIZE_OFFSET;  // skip the target spectrum's own header

   char* res = malloc(len);
   if (!res) {
      error("extract_no_encode_from_cache: malloc(%lu) failed",
            (unsigned long)len);
      return NULL;
   }
   memcpy(res, blk->cache + offset, len);
   return res;
}

/**
 * @brief Encodes a binary block using the specified encoding function and
 * algorithm.
 * @param blk A pointer to the `block_len_t` structure containing the binary
 * block to be encoded.
 * @param curr_dp A pointer to the `data_positions_t` structure containing the
 * positions of the data to be encoded.
 * @param source_fmt The format of the source data.
 * @param target_fmt The format of the target encoded data.
 * @param encode_fun A pointer to the encoding function to be used for encoding
 * the data.
 * @param scale_factor A float value representing the scale factor to be applied
 * during encoding.
 * @param target_fun A pointer to the algorithm function to be used for encoding
 * the data.
 * @return Returns 0 on success, and 1 on failure.
 *
 * @warning Caller is responsible for freeing the encoded_cache and
 *          encoded_cache_lens stored in the `block_len_t` structure after use.
 *          Previously allocated encoded_cache and encoded_cache_lens are freed
 *          internally before being overwritten.
 */
int encode_binary_block(block_len_t* blk, data_positions_t* curr_dp,
                        uint32_t source_fmt, uint32_t target_fmt,
                        encode_fun encode_fun, float scale_factor,
                        Algo target_fun) {
   if (blk->encoded_cache_len > 0 && blk->encoded_cache_fmt == target_fmt) {
      // Already encoded with the same format, no need to re-encode
      return 0;
   }
   uint64_t total_spec = curr_dp->total_spec;

   // Allocate memory for algo_args
   algo_args* a_args = malloc(sizeof(algo_args));
   if (!a_args) {
      error("encode_binary_block: Failed to allocate algo_args.\n");
      return 1;
   }

   a_args->ret_code = 0;  // Initialize return code to 0 (success).

   size_t algo_output_len = 0;
   char* decmp_binary = blk->cache;

   // Allocate a buffer to hold the encoded data. The size is determined by the
   // total length of the binary data to be encoded.
   char* buff = malloc(curr_dp->end_positions[total_spec - 1] -
                       curr_dp->start_positions[0]);
   if (!buff) {
      error(
          "encode_binary_block: Failed to allocate buffer for encoded data.\n");
      free(a_args);
      return 1;
   }

   // Allocate an array to hold the lengths of the encoded blocks for each
   // spectrum.
   size_t* res_lens = malloc(total_spec * sizeof(size_t));
   if (!res_lens) {
      error("encode_binary_block: Failed to allocate res_lens array.\n");
      free(a_args);
      free(buff);
      return 1;
   }
   uint64_t buff_off = 0;

   // Initialize the z_stream for encoding
   a_args->z = alloc_z_stream();
   if (!a_args->z) {
      error("encode_binary_block: Failed to allocate z_stream.\n");
      free(a_args);
      free(buff);
      free(res_lens);
      return 1;
   }

   // Dedicated inflate stream for any zlib decode paths (init once, reset per
   // block).
   a_args->z_inflate = alloc_z_stream_inflate();
   if (!a_args->z_inflate) {
      error("encode_binary_block: Failed to allocate inflate z_stream.\n");
      dealloc_z_stream(a_args->z);
      free(a_args);
      free(buff);
      free(res_lens);
      return 1;
   }
   a_args->dest_len = &algo_output_len;

   for (int i = 0; i < total_spec; i++) {
      size_t src_len = curr_dp->end_positions[i] - curr_dp->start_positions[i];

      // Empty <binary></binary>: compression skipped it, so the cache holds
      // no bytes for this spectrum. Calling target_fun with src_len==0 would
      // walk into the next spectrum's payload and shift every later index.
      if (src_len == 0) {
         res_lens[i] = 0;
         continue;
      }

      a_args->src = (char**)&decmp_binary;
      a_args->src_len = src_len;
      a_args->dest = (char **)(buff + buff_off);
      a_args->src_format = source_fmt;
      a_args->enc_fun = encode_fun;
      a_args->scale_factor = scale_factor;

      // Call the target function to encode the binary block and write it to the
      // output buffer
      target_fun((void*)a_args);

      if (a_args->ret_code != 0) {
         error(
             "encode_binary_block: Failed to encode binary block for spectrum "
             "%d.\n",
             i);
         dealloc_z_stream(a_args->z);
         dealloc_z_stream_inflate(a_args->z_inflate);
         free(a_args);
         free(buff);
         free(res_lens);
         return 1;
      }

      res_lens[i] = *a_args->dest_len;
      buff_off += *a_args->dest_len;
   }

   dealloc_z_stream(a_args->z);
   dealloc_z_stream_inflate(a_args->z_inflate);
   free(a_args);

   // Free previous encoded cache if present (avoid leak on re-encode)
   if (blk->encoded_cache) free(blk->encoded_cache);
   if (blk->encoded_cache_lens) free(blk->encoded_cache_lens);

   // Update the block structure with the encoded data and its format
   blk->encoded_cache = buff;
   blk->encoded_cache_fmt = target_fmt;
   blk->encoded_cache_len = buff_off;
   blk->encoded_cache_lens = res_lens;

   return 0;
}

/**
 * @brief Extracts a specific encoded block from the given `block_len_t`
 * structure.
 * @param blk A pointer to the `block_len_t` structure containing the encoded
 * blocks.
 * @param index The index of the block to extract.
 * @param out_len A pointer to a `size_t` variable where the length of the
 * extracted block will be stored.
 * @return A pointer to the extracted block on success, or `NULL` on failure.
 * @note The caller is responsible for freeing the returned buffer.
 */
char* extract_from_encoded_block(block_len_t* blk, long index,
                                 size_t* out_len) {
   size_t offset = 0;
   size_t len = blk->encoded_cache_lens[index];

   // Empty spectrum: return a freeable non-NULL pointer (callers do
   // free(res) unconditionally, and NULL would trip Python's "Failed to
   // extract" error path in _core.pyx).
   if (len == 0) {
      *out_len = 0;
      return malloc(1);
   }

   char* res = malloc(len);
   if (!res)
      return NULL;

   for (int i = 0; i < index; i++) offset += blk->encoded_cache_lens[i];

   memcpy(res, blk->encoded_cache + offset, len);

   *out_len = len;
   return res;
}

/**
 * @brief Extracts the m/z values for a given spectrum index from the input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param mz_binary_block_lens A pointer to a `block_len_queue_t` struct
 * containing the lengths of the m/z binary blocks.
 * @param mz_binary_blk_pos The offset within the input buffer where the m/z
 * binary blocks start.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param index The index of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted m/z
 * block will be stored.
 * @param encode An integer flag indicating whether to encode the extracted
 * block (1) or not (0).
 * @return A pointer to the extracted m/z block on success. `NULL` on error.
 * @note The caller is responsible for freeing the returned buffer.
 * @note Decompressed data is cached in the `block_len_t` node to avoid redundant decompression.
 */
char* extract_spectrum_mz(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                          block_len_queue_t* mz_binary_block_lens,
                          long mz_binary_blk_pos, divisions_t* divisions,
                          long index, size_t* out_len, int encode,
                          block_lru_t* lru) {
   data_positions_t* mz;
   long mz_off = 0;
   int division_index = 0;
   uint64_t start_position = 0;
   uint64_t end_position = 0;
   uint64_t src_len = 0;

   block_len_t* mz_blk_len;
   long mz_blk_offset;
   char* decmp_mz;
   char* res;

   // Determine what division contains mz and in which position
   for (division_index; division_index < divisions->n_divisions;
        division_index++) {
      division_t* curr = divisions->divisions[division_index];
      mz = curr->mz;
      if (index < mz_off + mz->total_spec) {
         mz_off = index - mz_off;
         start_position = curr->mz->start_positions[mz_off];
         end_position = curr->mz->end_positions[mz_off];
         src_len = end_position - start_position;
         break;
      }
      mz_off += mz->total_spec;
   }

   if (division_index >= divisions->n_divisions) {
      error("extract_spectrum_mz: Could not find division for spectrum index %ld.\n", index);
      return NULL;
   }

   mz_blk_len = get_block_by_index(mz_binary_block_lens, division_index);
   mz_blk_offset =
       mz_binary_blk_pos +
       get_block_offset_by_index(mz_binary_block_lens, division_index);

   if (!mz_blk_len->cache) {
      decmp_mz = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                    mz_blk_offset, mz_blk_len);
      if (decmp_mz == NULL) {
         error("extract_spectrum_mz: Failed to decompress mz block.\n");
         return NULL;
      }
      /* Undo the byte shuffle before the block enters the cache, so every
       * consumer of the cache sees plain samples. No-op for unshuffled files. */
      if (df->mz_shuffle_elem > 1 &&
          unshuffle_block_records(decmp_mz, mz_blk_len->original_size,
                                  df->mz_shuffle_elem) != 0) {
         error("extract_spectrum_mz: Failed to reverse m/z byte shuffle.\n");
         free(decmp_mz);
         return NULL;
      }
      mz_blk_len->cache = decmp_mz;
   } else
      decmp_mz = mz_blk_len->cache;
   lru_touch_block(lru, mz_blk_len);

   if (!encode) {
      // Fast path is valid only for lossless reads. For a lossy .msz the
      // cached payload is still in its transformed form (e.g. delta32), so
      // slicing it raw would both return wrong bytes and mis-walk the block
      // (the headers/sizes don't match a plain payload) — a crash. Lossy
      // no-encode reads fall through to encode_binary_block below, which
      // applies the inverse transform (df->target_mz_fun) without base64.
      if (df->target_mz_fun == algo_encode_lossless)
         return extract_no_encode_from_cache(mz_blk_len, mz, mz_off, out_len);

      // Lossy: target_mz_fun reconstructs samples into a fresh, header-less
      // buffer, so the writer must copy raw bytes (no_encode_no_header) — not
      // the header-popping no_encode_w_header that the lossless path uses.
      encode_binary_block(mz_blk_len, mz, df->source_mz_fmt, _no_encode_,
                          no_encode_no_header, df->mz_scale_factor,
                          df->target_mz_fun);
      res = extract_from_encoded_block(mz_blk_len, mz_off, out_len);
      return res;
   }

   encode_binary_block(mz_blk_len, mz, df->source_mz_fmt,
                       df->target_mz_format,
                       df->encode_source_compression_mz_fun,
                       df->mz_scale_factor, df->target_mz_fun);
   res = extract_from_encoded_block(mz_blk_len, mz_off, out_len);

   return res;
}

/**
 * @brief Extracts the intensity values for a given spectrum index from the
 * input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param inten_binary_block_lens A pointer to a `block_len_queue_t` struct
 * containing the lengths of the intensity binary blocks.
 * @param inten_binary_blk_pos The offset within the input buffer where the
 * intensity binary blocks start.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param index The index of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted
 * intensity block will be stored.
 * @param encode An integer flag indicating whether to encode the extracted
 * block (1) or not (0).
 * @return A pointer to the extracted intensity block on success. `NULL` on error.
 */
char* extract_spectrum_inten(char* input_map, ZSTD_DCtx* dctx,
                             data_format_t* df,
                             block_len_queue_t* inten_binary_block_lens,
                             long inten_binary_blk_pos, divisions_t* divisions,
                             long index, size_t* out_len, int encode,
                             block_lru_t* lru) {
   data_positions_t* inten;
   long inten_off = 0;
   int division_index = 0;
   uint64_t start_position = 0;
   uint64_t end_position = 0;
   uint64_t src_len = 0;

   block_len_t* inten_blk_len;
   long inten_blk_offset;
   char* decmp_inten;
   char* res;

   // Determine what division contains inten and in which position
   for (division_index; division_index < divisions->n_divisions;
        division_index++) {
      division_t* curr = divisions->divisions[division_index];
      inten = curr->inten;
      if (index < inten_off + inten->total_spec) {
         inten_off = index - inten_off;
         start_position = curr->inten->start_positions[inten_off];
         end_position = curr->inten->end_positions[inten_off];
         src_len = end_position - start_position;
         break;
      }
      inten_off += inten->total_spec;
   }

   if (division_index >= divisions->n_divisions) {
      error("extract_spectrum_inten: Could not find division for spectrum index %ld.\n", index);
      return NULL;
   }

   inten_blk_len = get_block_by_index(inten_binary_block_lens, division_index);
   inten_blk_offset =
       inten_binary_blk_pos +
       get_block_offset_by_index(inten_binary_block_lens, division_index);

   if (!inten_blk_len->cache) {
      decmp_inten =
          (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                             inten_blk_offset, inten_blk_len);
      if (decmp_inten == NULL) {
         error(
             "extract_spectrum_inten: Failed to decompress intensity block.\n");
         return NULL;
      }
      /* Undo the byte shuffle before the block enters the cache, so every
       * consumer of the cache sees plain samples. No-op for unshuffled files. */
      if (df->inten_shuffle_elem > 1 &&
          unshuffle_block_records(decmp_inten, inten_blk_len->original_size,
                                  df->inten_shuffle_elem) != 0) {
         error(
             "extract_spectrum_inten: Failed to reverse intensity byte "
             "shuffle.\n");
         free(decmp_inten);
         return NULL;
      }
      inten_blk_len->cache = decmp_inten;
   } else
      decmp_inten = inten_blk_len->cache;
   lru_touch_block(lru, inten_blk_len);

   if (!encode) {
      // Lossless-only fast path — see extract_spectrum_mz for the rationale.
      if (df->target_inten_fun == algo_encode_lossless)
         return extract_no_encode_from_cache(inten_blk_len, inten, inten_off,
                                             out_len);

      // Lossy: see extract_spectrum_mz — reconstructed samples are header-less.
      int ret = encode_binary_block(
          inten_blk_len, inten, df->source_inten_fmt, _no_encode_,
          no_encode_no_header, df->int_scale_factor, df->target_inten_fun);
      if (ret != 0) {
         error("extract_spectrum_inten: Failed to encode intensity block.\n");
         return NULL;
      }
      res = extract_from_encoded_block(inten_blk_len, inten_off, out_len);
      return res;
   }

   int ret = encode_binary_block(inten_blk_len, inten, df->source_inten_fmt,
                                 df->target_inten_format,
                                 df->encode_source_compression_inten_fun,
                                 df->int_scale_factor, df->target_inten_fun);
   if (ret != 0) {
      error("extract_spectrum_inten: Failed to encode intensity block.\n");
      return NULL;
   }

   res = extract_from_encoded_block(inten_blk_len, inten_off, out_len);

   return res;
}

/**
 * @brief Extracts the complete spectrum for a given index from the input map.
 * @param input_map The input buffer containing the compressed data.
 * @param dctx A pointer to a `ZSTD_DCtx` struct for decompression.
 * @param df A pointer to a `data_format_t` struct containing the data format
 * information.
 * @param xml_block_lens A pointer to a `block_len_queue_t` struct containing
 * the lengths of the XML blocks.
 * @param mz_binary_block_lens A pointer to a `block_len_queue_t` struct
 * containing the lengths of the m/z binary blocks.
 * @param inten_binary_block_lens A pointer to a `block_len_queue_t` struct
 * containing the lengths of the intensity binary blocks.
 * @param xml_pos The offset within the input buffer where the XML blocks start.
 * @param mz_pos The offset within the input buffer where the m/z binary blocks
 * start.
 * @param inten_pos The offset within the input buffer where the intensity
 * binary blocks start.
 * @param mz_fmt The format of the m/z values to be extracted.
 * @param inten_fmt The format of the intensity values to be extracted.
 * @param divisions A pointer to a `divisions_t` struct containing the division
 * information.
 * @param index The index of the spectrum to extract.
 * @param out_len A pointer to a `size_t` where the length of the extracted
 * spectrum will be stored.
 * @return A pointer to the extracted spectrum on success. `NULL` on error.
 */
char* extract_spectra(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                      block_len_queue_t* xml_block_lens,
                      block_len_queue_t* mz_binary_block_lens,
                      block_len_queue_t* inten_binary_block_lens, long xml_pos,
                      long mz_pos, long inten_pos, int mz_fmt, int inten_fmt,
                      divisions_t* divisions, long index, size_t* out_len,
                      block_lru_t* lru) {
   uint64_t spectrum_start;
   uint64_t spectrum_end;

   uint64_t xml_start;
   uint64_t xml_end;

   block_len_t* xml_blk_len;
   long xml_blk_offset;
   int division_index;
   char* res;
   division_index = determine_division_by_index(divisions, index);
   determine_spectrum_start_end(divisions, index, &spectrum_start,
                                &spectrum_end);

   res = calloc((spectrum_end - spectrum_start),
                2);  // Over-allocate as b64 may grow

   size_t start_xml_len = 0;
   char* spectrum_start_xml = extract_spectrum_start_xml(
       input_map, dctx, df, xml_block_lens, xml_pos, divisions, spectrum_start,
       spectrum_end, &start_xml_len, lru);
   memcpy(res, spectrum_start_xml, start_xml_len);
   *out_len += start_xml_len;
   free(spectrum_start_xml);

   size_t mz_len = 0;
   char* spectrum_mz =
       extract_spectrum_mz(input_map, dctx, df, mz_binary_block_lens, mz_pos,
                           divisions, index, &mz_len, TRUE, lru);

   if (spectrum_mz == NULL) {
      error(
          "extract_spectra: Failed to extract m/z values for spectrum index "
          "%ld.\n",
          index);
      free(res);
      return NULL;
   }
   memcpy(res + *out_len, spectrum_mz, mz_len);
   *out_len += mz_len;
   free(spectrum_mz);

   size_t inner_xml_len = 0;
   char* spectrum_inner_xml = extract_spectrum_inner_xml(
       input_map, dctx, df, xml_block_lens, xml_pos, divisions, spectrum_start,
       spectrum_end, &inner_xml_len, lru);

   if (spectrum_inner_xml == NULL) {
      error(
          "extract_spectra: Failed to extract inner XML for spectrum index "
          "%ld.\n",
          index);
      free(res);
      return NULL;
   }

   memcpy(res + *out_len, spectrum_inner_xml, inner_xml_len);
   *out_len += inner_xml_len;
   free(spectrum_inner_xml);

   size_t inten_len = 0;
   char* spectrum_inten =
       extract_spectrum_inten(input_map, dctx, df, inten_binary_block_lens,
                              inten_pos, divisions, index, &inten_len, TRUE,
                              lru);
   if (spectrum_inten == NULL) {
      error(
          "extract_spectra: Failed to extract intensity values for spectrum "
          "index %ld.\n",
          index);
      free(res);
      return NULL;
   }

   memcpy(res + *out_len, spectrum_inten, inten_len);
   *out_len += inten_len;
   free(spectrum_inten);

   size_t last_xml_len = 0;
   char* spectrum_last_xml = extract_spectrum_last_xml(
       input_map, dctx, df, xml_block_lens, xml_pos, divisions, spectrum_start,
       spectrum_end, &last_xml_len, lru);
   if (spectrum_last_xml == NULL) {
      error(
          "extract_spectra: Failed to extract last XML for spectrum index "
          "%ld.\n",
          index);
      free(res);
      return NULL;
   }

   memcpy(res + *out_len, spectrum_last_xml, last_xml_len);
   *out_len += last_xml_len;
   free(spectrum_last_xml);

   print("Extracted spectrum index %ld\n", index);
   return res;
}

/**
 * @brief Extracts selected spectra from an MSZ compressed file and writes them as mzML.
 * @param input_map The memory-mapped MSZ input file buffer.
 * @param input_filesize The total size of the input file in bytes.
 * @param indicies Array of spectrum indices to extract (may be overridden by ms_level or scans).
 * @param indicies_length Number of elements in the indicies array.
 * @param scans Array of scan numbers to extract (used when scans_length > 0).
 * @param scans_length Number of elements in the scans array.
 * @param ms_level MS level to filter by (0 means no MS level filtering).
 * @param output_fd The file descriptor to write the extracted mzML output to.
 * @note When ms_level != 0 or scans_length > 0, this function allocates a new
 *       indicies array internally and frees it before returning. The original
 *       indicies parameter is not freed in that case.
 */
void extract_msz(char* input_map, size_t input_filesize, long* indicies,
                 long indicies_length, uint32_t* scans, long scans_length,
                 uint16_t ms_level, int output_fd) {
   block_len_queue_t *xml_block_lens, *mz_binary_block_lens,
       *inten_binary_block_lens;
   footer_t* msz_footer;

   int n_divisions = 0;
   divisions_t* divisions;
   data_format_t* df;

   ZSTD_DCtx* dctx = alloc_dctx();

   if (dctx == NULL)
      error("decompress_routine: ZSTD Context failed.\n");

   df = get_header_df(input_map);

   parse_footer(&msz_footer, input_map, input_filesize, &xml_block_lens,
                &mz_binary_block_lens, &inten_binary_block_lens, &divisions,
                &n_divisions);

   if (ms_level != 0)  // MS level selected
   {
      indicies = map_ms_level_to_index_from_divisions(ms_level, divisions,
                                                      &indicies_length);
   }

   else if (scans_length > 0)  // Scan extraction selected
   {
      indicies = map_scans_to_index_from_divisions(scans, scans_length,
                                                   divisions, &indicies_length);
   }

   set_decompress_runtime_variables(df, msz_footer);

   if (n_divisions == 0) {
      warning("No divisions found in file, aborting...\n");
      return;
   }

   block_len_t *xml_blk_len, *mz_binary_blk_len, *inten_binary_blk_len;
   long xml_blk_offset, mz_blk_offset, inten_blk_offset;
   size_t buff_off = 0;
   char* buff;
   char *decmp_xml, *decmp_mz_binary, *decmp_inten_binary;
   division_t* curr_division = divisions->divisions[0];

   // Get mzML header (in first division):
   size_t header_len = 0;
   xml_blk_len = get_block_by_index(xml_block_lens, 0);
   xml_blk_offset = msz_footer->xml_pos;
   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      if (decmp_xml == NULL) {
         error("extract_msz: Failed to decompress XML block for mzML header.\n");
         return;
      }
      xml_blk_len->cache = decmp_xml;
   } else {
      decmp_xml = xml_blk_len->cache;
   }
   char* mzml_header =
       extract_mzml_header(decmp_xml, curr_division, &header_len);

   // Update spectrumList count attribute to match extracted spectra count
   size_t updated_header_len = 0;
   char* updated_header = update_spectrum_list_count(
       mzml_header, header_len, indicies_length, &updated_header_len);
   if (updated_header) {
      write_to_file(output_fd, updated_header, updated_header_len);
      free(updated_header);
   } else {
      // Fallback: write original header
      write_to_file(output_fd, mzml_header, header_len);
   }
   free(mzml_header);

   // Get spectra
   for (long i = 0; i < indicies_length; i++) {
      size_t spectra_len = 0;
      char* spectrum = extract_spectra(
          input_map, dctx, df, xml_block_lens, mz_binary_block_lens,
          inten_binary_block_lens, msz_footer->xml_pos,
          msz_footer->mz_binary_pos, msz_footer->inten_binary_pos,
          msz_footer->mz_fmt, msz_footer->inten_fmt, divisions, indicies[i],
          &spectra_len, NULL);
      write_to_file(output_fd, spectrum, spectra_len);
      free(spectrum);
   }

   // Get mzML footer (in last division):
   size_t footer_len = 0;
   xml_blk_len = get_block_by_index(xml_block_lens, divisions->n_divisions - 1);
   xml_blk_offset =
       msz_footer->xml_pos +
       get_block_offset_by_index(xml_block_lens, divisions->n_divisions - 1);
   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      if (decmp_xml == NULL) {
         error("extract_msz: Failed to decompress XML block for mzML footer.\n");
         return;
      }
      xml_blk_len->cache = decmp_xml;
   } else {
      decmp_xml = xml_blk_len->cache;
   }
   char* mzml_footer = extract_mzml_footer(decmp_xml, divisions, &footer_len);
   // print("%s\n", mzml_footer);
   write_to_file(output_fd, mzml_footer, footer_len);
   free(mzml_footer);

   // Free indicies if allocated by ms_level or scan mapping
   if (ms_level != 0 || scans_length > 0) {
      free(indicies);
   }

   ZSTD_freeDCtx(dctx);
   dealloc_block_len_queue(xml_block_lens);
   dealloc_block_len_queue(mz_binary_block_lens);
   dealloc_block_len_queue(inten_binary_block_lens);
   dealloc_df(df);
   dealloc_read_divisions(divisions);
}

/**
 * @brief Extracts selected spectra from an mzML file and writes the filtered output.
 * @param input_map The memory-mapped mzML input file buffer.
 * @param input_filesize The total size of the input file in bytes.
 * @param indicies Array of spectrum indices to extract (may be overridden by ms_level or scans).
 * @param indicies_length Number of elements in the indicies array.
 * @param scans Array of scan numbers to extract (used when scans_length > 0).
 * @param scans_length Number of elements in the scans array.
 * @param ms_level MS level to filter by (0 means no MS level filtering).
 * @param division The division containing the mzML file's position data.
 * @param output_fd The file descriptor to write the filtered mzML output to.
 * @note When `ms_level != 0` or `scans_length > 0`, this function allocates a new
 *       indicies array internally and frees it before returning.
 */
void extract_mzml_filtered(char* input_map, size_t input_filesize,
                           long* indicies, long indicies_length,
                           uint32_t* scans, long scans_length,
                           uint16_t ms_level, division_t* division,
                           int output_fd) {
   if (ms_level != 0) {
      indicies = map_ms_level_to_index(ms_level, division, 0, &indicies_length);
   } else if (scans_length > 0) {
      indicies =
          map_scan_to_index(scans, scans_length, division, 0, &indicies_length);
   }

   // Write header with updated spectrumList count
   size_t header_len = division->spectra->start_positions[0];
   size_t updated_header_len = 0;
   char* updated_header = update_spectrum_list_count(
       input_map, header_len, indicies_length, &updated_header_len);
   if (updated_header) {
      write_to_file(output_fd, updated_header, updated_header_len);
      free(updated_header);
   } else {
      // Fallback: write original header
      write_to_file(output_fd, input_map, header_len);
   }

   // Write spectra
   for (long i = 0; i < indicies_length; i++) {
      long idx = indicies[i];
      size_t start = division->spectra->start_positions[idx];
      size_t end = division->spectra->end_positions[idx];
      size_t len = end - start;
      write_to_file(output_fd, input_map + start, len);
      write_to_file(output_fd, "\n", 1);
   }

   // Write footer
   size_t last_spec_end =
       division->spectra->end_positions[division->spectra->total_spec - 1];
   size_t footer_len = input_filesize - last_spec_end;
   write_to_file(output_fd, input_map + last_spec_end, footer_len);

   // Cleanup allocated indices if necessary
   if (ms_level != 0 || scans_length > 0) {
      if (indicies)
         free(indicies);
   }
}