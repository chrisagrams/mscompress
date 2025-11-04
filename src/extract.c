#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"

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
   }
   return;
}

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

char* extract_mzml_header(char* blk, division_t* first_division,
                          size_t* out_len)
/*
 * Extract from [XML position 0 -> First spectra position 0]
 */
{
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

char* extract_spectrum_start_xml(char* input_map, ZSTD_DCtx* dctx,
                                 data_format_t* df,
                                 block_len_queue_t* xml_block_lens,
                                 long xml_pos, divisions_t* divisions,
                                 uint64_t spectrum_start, uint64_t spectrum_end,
                                 size_t* out_len)
/*
 * Extract from spectrum_start until xml->end_position (from <spectrum> to
 * <binary>)
 */
{
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index;
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

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;

   *out_len = xml_end_position - xml_start_position - xml_start_offset;
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset + xml_start_offset, *out_len);

   return res;
}

char* extract_spectrum_inner_xml(char* input_map, ZSTD_DCtx* dctx,
                                 data_format_t* df,
                                 block_len_queue_t* xml_block_lens,
                                 long xml_pos, divisions_t* divisions,
                                 uint64_t spectrum_start, uint64_t spectrum_end,
                                 size_t* out_len)
/*
 * Extract XML between binary sections (XML positions fully inside
 * spectrum_start and spectrum_end).
 */
{
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index;
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
            found = 0;
            break;
         }
         xml_sum += xml->end_positions[j] - xml->start_positions[j];
      }
   }

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;

   *out_len = xml_end_position - xml_start_position;
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset, *out_len);

   return res;
}

char* extract_spectrum_last_xml(char* input_map, ZSTD_DCtx* dctx,
                                data_format_t* df,
                                block_len_queue_t* xml_block_lens, long xml_pos,
                                divisions_t* divisions, uint64_t spectrum_start,
                                uint64_t spectrum_end, size_t* out_len)
/*
 * Extract final XML in spectrum after last binary.
 */
{
   char* res;

   char* decmp_xml;

   data_positions_t* xml;

   uint64_t xml_start_position;
   uint64_t xml_end_position;
   int division_index;
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

   xml_blk_len = get_block_by_index(xml_block_lens, division_index);
   xml_blk_offset =
       xml_pos + get_block_offset_by_index(xml_block_lens, division_index);

   if (!xml_blk_len->cache) {
      decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                     xml_blk_offset, xml_blk_len);
      xml_blk_len->cache = decmp_xml;
   } else
      decmp_xml = xml_blk_len->cache;

   *out_len = (xml_end_position - xml_start_position) -
              (xml_end_position - spectrum_end) + 1;  //+1 For newline
   res = malloc(*out_len);
   memcpy(res, decmp_xml + xml_buff_offset, *out_len);

   return res;
}

void encode_binary_block(block_len_t* blk, data_positions_t* curr_dp,
                         uint32_t source_fmt, uint32_t target_fmt,
                         encode_fun encode_fun, float scale_factor,
                         Algo target_fun) {
   if (blk->encoded_cache_len > 0 && blk->encoded_cache_fmt == target_fmt)
      return;
   uint64_t total_spec = curr_dp->total_spec;

   algo_args* a_args = malloc(sizeof(algo_args));
   size_t algo_output_len = 0;
   char* decmp_binary = blk->cache;
   char* buff = malloc(curr_dp->end_positions[total_spec - 1] -
                       curr_dp->start_positions[0]);
   size_t* res_lens = malloc(total_spec * sizeof(size_t));
   uint64_t buff_off = 0;
   a_args->z = alloc_z_stream();
   a_args->dest_len = &algo_output_len;

   for (int i = 0; i < total_spec; i++) {
      a_args->src = (char**)&decmp_binary;
      a_args->src_len = curr_dp->end_positions[i] - curr_dp->start_positions[i];
      a_args->dest = buff + buff_off;
      a_args->src_format = source_fmt;
      a_args->enc_fun = encode_fun;
      a_args->scale_factor = scale_factor;
      target_fun((void*)a_args);
      res_lens[i] = *a_args->dest_len;
      buff_off += *a_args->dest_len;
   }

   // free(a_args);

   blk->encoded_cache = buff;
   blk->encoded_cache_fmt = target_fmt;
   blk->encoded_cache_len = buff_off;
   blk->encoded_cache_lens = res_lens;
}

char* extract_from_encoded_block(block_len_t* blk, long index,
                                 size_t* out_len) {
   size_t offset = 0;
   size_t len = blk->encoded_cache_lens[index];
   char* res = malloc(len);

   for (int i = 0; i < index; i++) offset += blk->encoded_cache_lens[i];

   memcpy(res, blk->encoded_cache + offset, len);

   *out_len = len;
   return res;
}

char* extract_spectrum_mz(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                          block_len_queue_t* mz_binary_block_lens,
                          long mz_binary_blk_pos, divisions_t* divisions,
                          long index, size_t* out_len, int encode) {
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

   mz_blk_len = get_block_by_index(mz_binary_block_lens, division_index);
   mz_blk_offset =
       mz_binary_blk_pos +
       get_block_offset_by_index(mz_binary_block_lens, division_index);

   if (!mz_blk_len->cache) {
      decmp_mz = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                    mz_blk_offset, mz_blk_len);
      mz_blk_len->cache = decmp_mz;
   } else
      decmp_mz = mz_blk_len->cache;

   if (!encode) {
      encode_binary_block(
          mz_blk_len, mz, df->source_mz_fmt, _no_encode_,
          set_encode_fun(_no_encode_, _lossless_,
                         _64d_), /* Disables encoding for python library*/
          df->mz_scale_factor, df->target_mz_fun);
   } else {
      encode_binary_block(mz_blk_len, mz, df->source_mz_fmt,
                          df->target_mz_format,
                          df->encode_source_compression_mz_fun,
                          df->mz_scale_factor, df->target_mz_fun);
   }
   res = extract_from_encoded_block(mz_blk_len, mz_off, out_len);

   return res;
}

char* extract_spectrum_inten(char* input_map, ZSTD_DCtx* dctx,
                             data_format_t* df,
                             block_len_queue_t* inten_binary_block_lens,
                             long inten_binary_blk_pos, divisions_t* divisions,
                             long index, size_t* out_len, int encode) {
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

   // Determine what division contains iten and in which position
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

   inten_blk_len = get_block_by_index(inten_binary_block_lens, division_index);
   inten_blk_offset =
       inten_binary_blk_pos +
       get_block_offset_by_index(inten_binary_block_lens, division_index);

   if (!inten_blk_len->cache) {
      decmp_inten =
          (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                             inten_blk_offset, inten_blk_len);
      inten_blk_len->cache = decmp_inten;
   } else
      decmp_inten = inten_blk_len->cache;

   if (!encode) {
      encode_binary_block(
          inten_blk_len, inten, df->source_inten_fmt, _no_encode_,
          set_encode_fun(_no_encode_, _lossless_,
                         _64d_), /* Disables encoding for python library*/
          df->int_scale_factor, df->target_inten_fun);
   } else {
      encode_binary_block(inten_blk_len, inten, df->source_inten_fmt,
                          df->target_inten_format,
                          df->encode_source_compression_inten_fun,
                          df->int_scale_factor, df->target_inten_fun);
   }

   res = extract_from_encoded_block(inten_blk_len, inten_off, out_len);

   return res;
}

char* extract_spectra(char* input_map, ZSTD_DCtx* dctx, data_format_t* df,
                      block_len_queue_t* xml_block_lens,
                      block_len_queue_t* mz_binary_block_lens,
                      block_len_queue_t* inten_binary_block_lens, long xml_pos,
                      long mz_pos, long inten_pos, int mz_fmt, int inten_fmt,
                      divisions_t* divisions, long index, size_t* out_len) {
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
       spectrum_end, &start_xml_len);
   memcpy(res, spectrum_start_xml, start_xml_len);
   *out_len += start_xml_len;

   size_t mz_len = 0;
   char* spectrum_mz =
       extract_spectrum_mz(input_map, dctx, df, mz_binary_block_lens, mz_pos,
                           divisions, index, &mz_len, TRUE);
   memcpy(res + *out_len, spectrum_mz, mz_len);
   *out_len += mz_len;

   size_t inner_xml_len = 0;
   char* spectrum_inner_xml = extract_spectrum_inner_xml(
       input_map, dctx, df, xml_block_lens, xml_pos, divisions, spectrum_start,
       spectrum_end, &inner_xml_len);
   memcpy(res + *out_len, spectrum_inner_xml, inner_xml_len);
   *out_len += inner_xml_len;

   size_t inten_len = 0;
   char* spectrum_inten =
       extract_spectrum_inten(input_map, dctx, df, inten_binary_block_lens,
                              inten_pos, divisions, index, &inten_len, TRUE);
   memcpy(res + *out_len, spectrum_inten, inten_len);
   *out_len += inten_len;

   size_t last_xml_len = 0;
   char* spectrum_last_xml = extract_spectrum_last_xml(
       input_map, dctx, df, xml_block_lens, xml_pos, divisions, spectrum_start,
       spectrum_end, &last_xml_len);
   memcpy(res + *out_len, spectrum_last_xml, last_xml_len);
   *out_len += last_xml_len;

   print("Extracted spectrum index %ld\n", index);
   return res;
}

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
   decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                  xml_blk_offset, xml_blk_len);
   xml_blk_len->cache = decmp_xml;  // Cache decompressed block
   char* mzml_header =
       extract_mzml_header(decmp_xml, curr_division, &header_len);
   // print("%s\n", mzml_header);
   write_to_file(output_fd, mzml_header, header_len);

   // Get spectra
   for (long i = 0; i < indicies_length; i++) {
      size_t spectra_len = 0;
      char* spectrum = extract_spectra(
          input_map, dctx, df, xml_block_lens, mz_binary_block_lens,
          inten_binary_block_lens, msz_footer->xml_pos,
          msz_footer->mz_binary_pos, msz_footer->inten_binary_pos,
          msz_footer->mz_fmt, msz_footer->inten_fmt, divisions, indicies[i],
          &spectra_len);
      write_to_file(output_fd, spectrum, spectra_len);
      free(spectrum);
   }

   // Get mzML footer (in last division):
   size_t footer_len = 0;
   xml_blk_len = get_block_by_index(xml_block_lens, divisions->n_divisions - 1);
   xml_blk_offset =
       msz_footer->xml_pos +
       get_block_offset_by_index(xml_block_lens, divisions->n_divisions - 1);
   decmp_xml = (char*)decmp_block(df->xml_decompression_fun, dctx, input_map,
                                  xml_blk_offset, xml_blk_len);
   xml_blk_len->cache = decmp_xml;  // Cache decompressed block
   char* mzml_footer = extract_mzml_footer(decmp_xml, divisions, &footer_len);
   // print("%s\n", mzml_footer);
   write_to_file(output_fd, mzml_footer, footer_len);
}