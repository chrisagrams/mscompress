/**
 * @file preprocess.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to prepare the software and preprocess input
 * files for compression/decompression.
 * @version 0.0.1
 * @date 2021-12-21
 *
 * @copyright
 *
 */

#include <assert.h>
#include <ctype.h>  // for isspace
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"
#include "yxml.h"

#define parse_acc_to_int(attrbuff)                                            \
   atoi(attrbuff + 3) /* Convert an accession to an integer by removing 'MS:' \
                         substring and calling atoi() */

/* === Start of allocation and deallocation helper functions === */

/**
 * @brief Allocates and initializes a yxml parser instance with an internal buffer.
 * @return A pointer to the initialized yxml_t parser.
 * @note The caller is responsible for freeing the returned pointer with free().
 * @warning Calls error() and aborts on malloc failure.
 */
// TODO: Remove yxml dependency since we no longer use it.
yxml_t* alloc_yxml() {
   yxml_t* xml = malloc(sizeof(yxml_t) + BUFSIZE);

   if (xml == NULL)
      error("malloc failure in alloc_yxml().\n");

   yxml_init(xml, xml + 1, BUFSIZE);
   return xml;
}

/**
 * @brief Allocates a zeroed data_format_t struct with populated field set to 0.
 * @return A pointer to the newly allocated data_format_t struct.
 * @note The caller is responsible for freeing the returned pointer with dealloc_df().
 * @warning Calls error() and aborts on malloc failure.
 */
data_format_t* alloc_df() {
   data_format_t* df = malloc(sizeof(data_format_t));

   if (df == NULL)
      error("alloc_df: malloc failure.\n");
   df->populated = 0;
   return df;
}

/**
 * @brief Deallocates a `data_format_t` struct and its memory.
 * @param df A pointer to the `data_format_t` struct to be deallocated.
 * 
 * TODO: return value for error handling?
 */
void dealloc_df(data_format_t* df) {
   if (df)
      free(df);
   else
      error("dealloc_df: df is NULL.\n");
}

/**
 * @brief Populates the target fields of a `data_format_t` struct.
 * @param df A pointer to the `data_format_t` struct to be populated.
 * @param target_xml_format The target XML format to be set in the `data_format_t` struct.
 * @param target_mz_format The target m/z format to be set in the `data_format_t` struct.
 * @param target_inten_format The target intensity format to be set in the `data_format_t` struct.
 */
void populate_df_target(data_format_t* df, int target_xml_format,
                        int target_mz_format, int target_inten_format) {
   df->target_xml_format = target_xml_format;
   df->target_mz_format = target_mz_format;
   df->target_inten_format = target_inten_format;
}

/**
 * @brief Allocates and populates a `data_positions_t` struct to store the start and end positions of spectra, XML, m/z, or intensity data within the .mzML file.
 * @param total_spec The total number of spectra in the .mzML file, used to determine the size of the start and end position arrays.
 * @return A populated `data_positions_t` struct with allocated start and end position arrays on success. NULL on error.
 */
data_positions_t* alloc_dp(int total_spec) {
   if (total_spec < 0)
      error("alloc_dp: total_spec is less than 0!\n");
   if (total_spec > 1000000)  // Realistically, this should never happen
      warning("alloc_dp: total_spec is greater than 1,000,000!\n");
   if (total_spec < 0)
      error("alloc_dp: total_spec is less than 0!\n");

   data_positions_t* dp = malloc(sizeof(data_positions_t));

   if (dp == NULL)
      error("alloc_dp: malloc failure.\n");

   if (total_spec == 0) {
      dp->total_spec = 0;
      dp->file_end = 0;
      dp->start_positions = NULL;
      dp->end_positions = NULL;
   }

   dp->total_spec = total_spec;
   dp->file_end = 0;
   dp->start_positions = malloc(sizeof(uint64_t) * total_spec * 2);
   dp->end_positions = malloc(sizeof(uint64_t) * total_spec * 2);

   if (dp->start_positions == NULL || dp->end_positions == NULL) {
      error("alloc_dp: malloc failure.\n");
      return NULL;
   }

   return dp;
}

/**
 * @brief Deallocates a `data_positions_t` struct and its memory.
 * @param dp A pointer to the `data_positions_t` struct to be deallocated.
 */
void dealloc_dp(data_positions_t* dp) {
   if (dp) {
      if (dp->start_positions)
         free(dp->start_positions);
      else
         error("dealloc_dp: dp->start_positions is null.\n");
      if (dp->end_positions)
         free(dp->end_positions);
      else
         error("dealloc_dp: dp->end_positions is null.\n");
      free(dp);
   } else
      error("dealloc_dp: dp is null.\n");
}

/**
 * @brief Deallocates a `division_t` struct and its memory / internal structures.
 * @param div A pointer to the `division_t` struct to be deallocated.
 */
void dealloc_division(division_t* div) {
   if (div == NULL) return;
   if (div->spectra != NULL) { dealloc_dp(div->spectra); div->spectra = NULL; }
   if (div->xml != NULL) { dealloc_dp(div->xml); div->xml = NULL; }
   if (div->mz != NULL) { dealloc_dp(div->mz); div->mz = NULL; }
   if (div->inten != NULL) { dealloc_dp(div->inten); div->inten = NULL; }
   if (div->scans != NULL) { free(div->scans); div->scans = NULL; }
   if (div->ms_levels != NULL) { free(div->ms_levels); div->ms_levels = NULL; }
   if (div->ret_times != NULL) { free(div->ret_times); div->ret_times = NULL; }
   free(div);
}

/**
 * @brief Deallocates a `divisions_t` struct and its children `division_t` structs.
 * @param divisions A pointer to the `divisions_t` struct to be deallocated.
 */
void dealloc_divisions(divisions_t* divisions) {
   if (divisions == NULL) return;
   if (divisions->divisions != NULL) {
      for (int i = 0; i < divisions->n_divisions; i++) {
         if (divisions->divisions[i] != NULL)
            dealloc_division(divisions->divisions[i]);
      }
      free(divisions->divisions);
      divisions->divisions = NULL;
   }
   free(divisions);
}

/**
 * @brief Deallocates a `division_t` struct and its memory / internal structures, without deallocating the arrays within the `data_positions_t` structs (those reference mmap'd memory from read_dp/read_uint*_arr).
 * @param div A pointer to the `division_t` struct to be deallocated.
 */
void dealloc_read_division(division_t* div) {
   if (div == NULL) return;
   /* Only free the data_positions_t structs, not the arrays they point to
      (those reference mmap'd memory from read_dp/read_uint*_arr). */
   if (div->spectra != NULL) { free(div->spectra); div->spectra = NULL; }
   if (div->xml != NULL) { free(div->xml); div->xml = NULL; }
   if (div->mz != NULL) { free(div->mz); div->mz = NULL; }
   if (div->inten != NULL) { free(div->inten); div->inten = NULL; }
   /* scans, ms_levels, ret_times point to mmap'd data - don't free */
   free(div);
}

/**
 * @brief Deallocates a `divisions_t` struct and its children `division_t` structs, without deallocating the arrays within the `data_positions_t` structs (those reference mmap'd memory from read_dp/read_uint*_arr).
 * @param divisions A pointer to the `divisions_t` struct to be deallocated.  
 */
void dealloc_read_divisions(divisions_t* divisions) {
   if (divisions == NULL) return;
   if (divisions->divisions != NULL) {
      for (int i = 0; i < divisions->n_divisions; i++) {
         if (divisions->divisions[i] != NULL)
            dealloc_read_division(divisions->divisions[i]);
      }
      free(divisions->divisions);
      divisions->divisions = NULL;
   }
   free(divisions);
}

/**
 * @brief Allocates an array of data_positions_t pointers, each initialized via alloc_dp with total_spec set to 0.
 * @param len The number of data_positions_t pointers to allocate (must be >= 1).
 * @param total_spec The capacity of each data_positions_t (must be >= 1).
 * @return A pointer to the array of data_positions_t pointers.
 * @note The caller is responsible for freeing the returned array with free_ddp().
 * @warning Calls error() and aborts on invalid arguments or malloc failure.
 */
data_positions_t** alloc_ddp(int len, int total_spec) {
   if (len < 1)
      error("alloc_ddp: len is less than 1.\n");
   if (total_spec < 1)
      error("alloc_ddp: total_spec is less than 1.\n");
   if (total_spec > 1000000)  // Realistically, this should never happen
      warning("alloc_ddp: total_spec is greater than 1,000,000!\n");

   data_positions_t** r;

   int i;

   r = malloc(len * sizeof(data_positions_t*));

   if (r == NULL)
      error("alloc_ddp: malloc failure.\n");

   for (i = 0; i < len; i++) {
      r[i] = alloc_dp(total_spec);
      r[i]->total_spec = 0;
   }

   return r;
}

/**
 * @brief Frees an array of data_positions_t pointers and their contents.
 * @param ddp The array of data_positions_t pointers to free.
 * @param divisions The number of elements in the ddp array (must be >= 1).
 * @warning Calls error() and aborts if ddp is NULL or divisions < 1.
 */
void free_ddp(data_positions_t** ddp, int divisions) {
   int i;

   if (divisions < 1)
      error("free_ddp: divisions is less than 1.\n");

   if (ddp) {
      for (i = 0; i < divisions; i++) dealloc_dp(ddp[i]);

      free(ddp);
   } else
      error("free_ddp: ddp is null.\n");
}

/**
 * @brief Allocates a division_t struct with internal data_positions_t arrays for spectra, XML, m/z, and intensity data.
 * @param n_xml The number of XML position entries to allocate.
 * @param n_mz The number of m/z position entries to allocate (also used for spectra, scans, and ms_levels).
 * @param n_inten The number of intensity position entries to allocate.
 * @return A pointer to the newly allocated division_t struct with all total_spec counters set to 0.
 * @note The caller is responsible for freeing the returned struct with dealloc_division().
 * @note ret_times is initialized to NULL; the caller must allocate it separately if needed.
 * @warning Calls error() and aborts on malloc failure.
 */
division_t* alloc_division(size_t n_xml, size_t n_mz, size_t n_inten) {
   division_t* d = malloc(sizeof(division_t));

   if (d == NULL)
      error("alloc_division: malloc failure.\n");

   d->spectra = alloc_dp(n_mz);
   d->xml = alloc_dp(n_xml);
   d->mz = alloc_dp(n_mz);
   d->inten = alloc_dp(n_inten);
   d->size = 0;

   d->scans = malloc(n_mz * sizeof(uint32_t));
   d->ms_levels = malloc(n_mz * sizeof(uint16_t));
   d->ret_times = NULL;

   if (d->spectra == NULL || d->xml == NULL || d->mz == NULL ||
       d->inten == NULL)
      error("alloc_division: malloc failure.\n");

   d->spectra->total_spec = 0;
   d->xml->total_spec = 0;
   d->mz->total_spec = 0;
   d->inten->total_spec = 0;

   return d;
}

/* === End of allocation and deallocation helper functions === */

/* === Start of XML traversal functions === */

/**
 * @brief Map an accession number to the data_format_t struct.
 *
 * This function populates the original compression method, m/z data array
 * format, and intensity data array format by interpreting MS CV accession
 * numbers encountered during XML traversal.
 *
 * @param acc A parsed integer of an accession attribute (expanded by parse_acc_to_int).
 * @param current_type A pass-by-reference variable indicating whether the traversal
 *        is within an m/z or intensity array. Updated by this function.
 * @param df An allocated, partially populated data_format_t struct to be further populated.
 * @return 1 if data_format_t struct is fully populated (both m/z and intensity formats detected), 0 otherwise.
 */
int map_to_df(int acc, int* current_type, data_format_t* df)
{
   if (acc >= _mass_ && acc <= _no_comp_) {
      switch (acc) {
         case _intensity_:
            *current_type = _intensity_;
            break;
         case _mass_:
            *current_type = _mass_;
            break;
         case _zlib_:
            df->source_compression = _zlib_;
            break;
         case _no_comp_:
            df->source_compression = _no_comp_;
            break;
         default:
            if (acc >= _32i_ && acc <= _64d_) {
               if (*current_type == _intensity_) {
                  df->source_inten_fmt = acc;
                  df->populated++;
               } else if (*current_type == _mass_) {
                  df->source_mz_fmt = acc;
                  df->populated++;
               }
               if (df->populated >= 2) {
                  return 1;
               }
            }
            break;
      }
   }
   return 0;
}

/**
 * @brief Detect the data type and encoding within a .mzML file.
 *
 * Traverses the XML structure of the mzML file using yxml to identify the source
 * compression method, m/z data format, intensity data format, and total spectrum count.
 * Since these data types and encodings are consistent throughout the entire .mzML
 * document, the function stops its traversal once all fields of the data_format_t
 * struct are filled.
 *
 * @param input_map A memory-mapped pointer to the .mzML file contents.
 * @return A populated data_format_t struct on success, NULL on failure (parse error or incomplete data).
 * @note The caller is responsible for freeing the returned pointer with dealloc_df().
 * @note Returns a malloc'd data_format_t that the caller owns.
 */
data_format_t* pattern_detect(char* input_map)
{
   data_format_t* df = alloc_df();

   yxml_t* xml = alloc_yxml();

   char attrbuf[11] = {'\0'}, *attrcur = NULL,
        *tmp = NULL; /* Length of a accession tag is at most 10 characters,
                        leave room for null terminator. */

   int in_cvParam =
       0; /* Boolean representing if currently inside of cvParam tag. */
   int current_type =
       0; /* A pass-by-reference variable to indicate to map_to_df of current
             binary data array type (m/z or intensity) */

   for (; *input_map; input_map++) {
      yxml_ret_t r = yxml_parse(xml, *input_map);
      if (r < 0) {
         free(xml);
         free(df);
         return NULL;
      }
      switch (r) {
         case YXML_ELEMSTART:
            if (strcmp(xml->elem, "cvParam") == 0)
               in_cvParam = 1;
            break;

         case YXML_ELEMEND:
            if (strcmp(xml->elem, "cvParam") == 0)
               in_cvParam = 0;
            break;

         case YXML_ATTRSTART:
            if (in_cvParam && strcmp(xml->attr, "accession") == 0)
               attrcur = attrbuf;
            else if (strcmp(xml->attr, "count") == 0)
               attrcur = attrbuf;
            break;

         case YXML_ATTRVAL:
            if (!in_cvParam || !attrcur)
               break;
            tmp = xml->data;
            while (*tmp && attrcur < attrbuf + sizeof(attrbuf))
               *(attrcur++) = *(tmp++);
            if (attrcur == attrbuf + sizeof(attrbuf)) {
               free(xml);
               free(df);
               return NULL;
            }
            *attrcur = 0;
            break;

         case YXML_ATTREND:
            if (attrcur && (strcmp(xml->elem, "spectrumList") == 0) &&
                (strcmp(xml->attr, "count") == 0)) {
               df->source_total_spec = atoi(attrbuf);
               attrcur = NULL;
            } else if (in_cvParam && attrcur) {
               if (map_to_df(parse_acc_to_int(attrbuf), &current_type, df)) {
                  free(xml);
                  return df;
               }
               attrcur = NULL;
            }
            break;

         default:
            /* TODO: handle errors. */
            break;
      }
   }
   free(xml);
   free(df);
   return NULL;
}

/**
 * @brief Validates that an array of file positions is non-negative and monotonically non-decreasing.
 * @param arr The array of uint64_t positions to validate.
 * @param len The number of elements in the array.
 * @return 0 if all positions are valid, 1 if a validation error is detected.
 */
int validate_positions(uint64_t* arr, int len) {
   int i;
   for (i = 0; i < len; i++) {
      if (arr[i] < 0) {
         warning("validate_positions: negative position detected.\n");
         return 1;
      }
      if (i > len) {
         warning("validate_positions: position %d out of bounds out of %d\n", i,
                 len);
         return 1;
      }
      if ((i + 1 < len) && arr[i] > arr[i + 1]) {
         warning("validate_positions: position %d is greater than %d\n", i,
                 i + 1);
         warning("validate_positions: arr[i] = %d | arr[i+1] = %d\n", arr[i],
                 arr[i + 1]);
         return 1;
      }
   }
   return 0;
}

/**
 * @brief Finds the start of the next <spectrum> XML element in the input.
 * @param ptr A pointer into the mzML file content to search from.
 * @return A pointer to the beginning of the "<spectrum " tag, or NULL if not found.
 */
char* get_spectrum_start(char* ptr) {
   char* res = strstr(ptr, "<spectrum ");
   if (res == NULL)
      warning("Could not find next spectrum.\n");
   return res;
}

/**
 * @brief Finds the end of the current </spectrum> XML element in the input.
 * @param ptr A pointer into the mzML file content to search from.
 * @return A pointer to the character immediately after the "</spectrum>" closing tag, or NULL if not found.
 */
char* get_spectrum_end(char* ptr) {
   char* res = strstr(ptr, "</spectrum>") + strlen("</spectrum>");
   if (res == NULL)
      warning("Could not find end of spectrum.\n");
   return res;
}

/**
 * @brief Finds the start of the binary data content within a <binary> XML element.
 * @param ptr A pointer into the mzML file content to search from.
 * @return A pointer to the first character after the "<binary>" opening tag, or NULL if not found.
 */
char* get_binary_start(char* ptr) {
   char* res = strstr(ptr, "<binary>") + strlen("<binary>");
   if (res == NULL)
      warning("Could not find start of binary.\n");
   return res;
}

/**
 * @brief Finds the end of the binary data content at the </binary> XML closing tag.
 * @param ptr A pointer into the mzML file content to search from.
 * @return A pointer to the start of the "</binary>" closing tag, or NULL if not found.
 */
char* get_binary_end(char* ptr) {
   char* res = strstr(ptr, "</binary>");
   if (res == NULL)
      warning("Could not find end of binary.\n");
   return res;
}

/**
 * @brief Extracts the MS level from a spectrum XML block by finding the MS:1000511 accession.
 * @param spectrum_start Pointer to the start of the spectrum XML block.
 * @return The MS level as a long integer (e.g., 1 for MS1, 2 for MS2). Returns 0 on failure.
 */
long get_ms_level(char* spectrum_start) {
   char* ptr =
       strstr(spectrum_start, "\"MS:1000511\"") + sizeof("\"MS:1000511\"");
   if (ptr == NULL)
      return 0;
   ptr = strstr(ptr, "value=\"") + sizeof("value=\"") - 1;
   char* e = strstr(ptr, "\"");
   if (e == NULL)
      return 0;
   return strtol(ptr, &e, 10);
}

/**
 * @brief Extracts the scan number from a spectrum XML block by parsing the "scan=" attribute.
 * @param spectrum_start Pointer to the start of the spectrum XML block.
 * @return The scan number as a long integer. Returns 0 on failure.
 */
long get_scan(char* spectrum_start) {
   char* ptr = strstr(spectrum_start, "scan=") + sizeof("scan=") - 1;
   char* e = strstr(ptr, "\"");
   if (e == NULL)
      return 0;
   return strtol(ptr, &e, 10);
}

/**
 * @brief Extract retention time (in seconds) from spectrum XML block.
 * @param spectrum_start Pointer to the start of the spectrum XML block.
 * @return Retention time as a float. Returns 0 on failure.
 */
float get_ret_time(char* spectrum_start) {
   // Find the position of the retention time cvParam
   char* ptr = strstr(spectrum_start, "accession=\"MS:1000016\"") +
               sizeof("accession=\"MS:1000016\"");
   // Return 0 if not found
   if (ptr == NULL)
      return 0;

   // Move the pointer to the value attribute
   ptr = strstr(ptr, "value=\"") + sizeof("value=\"") - 1;
   char* e = strstr(ptr, "\"");
   if (e == NULL)
      return 0;

   // Convert the retention time string to float
   float retention_time = strtof(ptr, &e);

   // Find the unit of the retention time
   ptr = strstr(e, "unitAccession=\"") + sizeof("unitAccession=\"") - 1;
   e = strstr(ptr, "\"");
   if (e == NULL)
      return 0;

   // Check if the unit is minutes and convert to seconds if necessary
   if (strncmp(ptr, "UO:0000031", e - ptr) == 0) {
      retention_time *= 60.0f;  // Convert minutes to seconds
   }
   return retention_time;
}

/**
 * @brief Scans an mzML file to locate all spectra, binary data positions, and optional metadata.
 *
 * Iterates through the memory-mapped mzML file, recording the start and end byte offsets
 * of each spectrum, its m/z binary data, intensity binary data, and intervening XML. Also
 * optionally extracts scan numbers, MS levels, and retention times based on the flags parameter.
 *
 * @param input_map A memory-mapped pointer to the mzML file content.
 * @param df A populated data_format_t struct containing the expected total spectrum count.
 *           May be modified (source_total_spec reduced) if fewer spectra are found.
 * @param end The byte offset of the end of the file (used as the final XML end position).
 * @param flags Bitmask of extraction flags: SCANNUM, MSLEVEL, RETTIME.
 * @return A malloc'd division_t containing all position data and metadata arrays, or NULL on error.
 * @note The caller is responsible for freeing the returned struct with dealloc_division().
 */
division_t* scan_mzml(char* input_map, data_format_t* df, long end, int flags) {
   if (input_map == NULL || df == NULL) {
      warning("scan_mzml: NULL pointer passed in.\n");
      return NULL;
   }
   if (end < 0) {
      error("scan_mzml: end position is negative.\n");
      return NULL;
   }

   data_positions_t *spectra_dp, *mz_dp, *inten_dp, *xml_dp;

   spectra_dp = alloc_dp(df->source_total_spec);
   xml_dp = alloc_dp(df->source_total_spec * 2);
   mz_dp = alloc_dp(df->source_total_spec);
   inten_dp = alloc_dp(df->source_total_spec);

   uint32_t* scans = (uint32_t*)calloc(df->source_total_spec, sizeof(uint32_t));
   uint16_t* ms_levels =
       (uint16_t*)calloc(df->source_total_spec, sizeof(uint16_t));
   float* ret_times = (float*)calloc(df->source_total_spec, sizeof(float));

   if (xml_dp == NULL || mz_dp == NULL || inten_dp == NULL) {
      warning("scan_mzml: failed to allocate memory.\n");
      return NULL;
   }

   char* ptr = input_map;

   int spec_curr = 0, mz_curr = 0, inten_curr = 0, xml_curr = 0;

   long curr_scan = 0;
   long curr_ms_level = 0;

   char* e;

   int bound = df->source_total_spec * 2;

   // xml base case
   xml_dp->start_positions[xml_curr] = 0;

   while (ptr) {
      if (mz_curr + inten_curr == bound)
         break;

      if (xml_curr >= bound || mz_curr >= df->source_total_spec ||
          inten_curr >= df->source_total_spec)  // We cannot continue if we have
                                                // reached the end of the array
      {
         warning(
             "scan_mzml: index out of bounds. xml_curr: %d, mz_curr: %d, "
             "inten_curr: %d\n",
             xml_curr, mz_curr, inten_curr);
         return NULL;
      }

      ptr = get_spectrum_start(
          ptr);  // Ptr now points to start of spectrum on success.
      if (ptr == NULL)
         break;  // We can't find the next spectrum, most likely an incomplete
                 // mzML file. Break and handle accordingly.

      spectra_dp->start_positions[spec_curr] = ptr - input_map;

      // From here, get any metadata we want to extract from the spectrum
      if (flags & SCANNUM) {  // If we want to extract scan numbers
         scans[spec_curr] = get_scan(ptr);
         if (ptr == NULL)
            return NULL;
      }

      if (flags & MSLEVEL) {  // If we want to extract ms levels
         ms_levels[spec_curr] = get_ms_level(ptr);
         if (ptr == NULL)
            return NULL;
      }

      if (flags & RETTIME) {  // If we want to extract ret_times
         ret_times[spec_curr] = get_ret_time(ptr);
         if (ptr == NULL)
            return NULL;
      }

      // Now, get the binaries and set the start and end positions
      ptr = get_binary_start(ptr);
      if (ptr == NULL)
         return NULL;
      mz_dp->start_positions[mz_curr] = ptr - input_map;
      xml_dp->end_positions[xml_curr++] = mz_dp->start_positions[mz_curr];

      ptr = get_binary_end(ptr);
      if (ptr == NULL)
         return NULL;
      mz_dp->end_positions[mz_curr] = ptr - input_map;
      xml_dp->start_positions[xml_curr] = mz_dp->end_positions[mz_curr];

      mz_curr++;

      ptr = get_binary_start(ptr);
      if (ptr == NULL)
         return NULL;
      inten_dp->start_positions[inten_curr] = ptr - input_map;
      xml_dp->end_positions[xml_curr++] = inten_dp->start_positions[inten_curr];

      ptr = get_binary_end(ptr);
      if (ptr == NULL)
         return NULL;
      inten_dp->end_positions[inten_curr] = ptr - input_map;
      xml_dp->start_positions[xml_curr] = inten_dp->end_positions[inten_curr];

      inten_curr++;

      ptr = get_spectrum_end(ptr);
      if (ptr == NULL)
         return NULL;

      spectra_dp->end_positions[spec_curr] = ptr - input_map;
      spec_curr++;
   }

   if (xml_curr != bound || mz_curr != df->source_total_spec ||
       inten_curr !=
           df->source_total_spec)  // If we haven't found all the binary data,
                                   // we have an incomplete mzML file. Treat the
                                   // rest as text.
   {
      warning(
          "scan_mzml: did not find all binary data. xml_curr: %d, mz_curr: %d, "
          "inten_curr: %d\n",
          xml_curr, mz_curr, inten_curr);
      warning("Expected %d spectra, found %d. Continuing...\n",
              df->source_total_spec, spec_curr);
      df->source_total_spec =
          spec_curr;  // Reset source_total_spec to the value actually found.
   }
   // xml base case
   xml_dp->end_positions[xml_curr] = end;
   xml_curr++;
   xml_dp->total_spec = xml_curr;

   mz_dp->total_spec = df->source_total_spec;
   inten_dp->total_spec = df->source_total_spec;

   mz_dp->file_end = inten_dp->file_end = xml_dp->file_end = end;

   // Sanity check
   if (validate_positions(mz_dp->start_positions, mz_dp->total_spec) ||
       validate_positions(mz_dp->end_positions, mz_dp->total_spec) ||
       validate_positions(inten_dp->start_positions, inten_dp->total_spec) ||
       validate_positions(inten_dp->end_positions, inten_dp->total_spec) ||
       validate_positions(xml_dp->start_positions, xml_dp->total_spec) ||
       validate_positions(xml_dp->end_positions, xml_dp->total_spec)) {
      warning("scan_mzml: validate_positions failed.\n");
      return NULL;
   }

   // Create division_t

   division_t* div = (division_t*)malloc(sizeof(division_t));
   if (div == NULL) {
      warning("scan_mzml: failed to allocate division_t.\n");
      return NULL;
   }
   div->spectra = spectra_dp;
   div->xml = xml_dp;
   div->mz = mz_dp;
   div->inten = inten_dp;
   div->size = end;  // Size is the end of the file

   div->scans = scans;
   div->ms_levels = ms_levels;
   div->ret_times = ret_times;

   return div;
}

/**
 * @brief Extracts a single spectrum from a division into a new division_t struct.
 *
 * Creates a new division containing only the specified spectrum's data positions (m/z, intensity,
 * and surrounding XML), along with the file header XML and footer XML. The new division's XML,
 * m/z, and intensity data_positions_t arrays are structured to preserve the expected interleaved
 * layout for downstream processing.
 *
 * @param div The source division_t containing all spectra.
 * @param index The zero-based index of the spectrum to extract.
 * @return A malloc'd division_t containing positions for the single extracted spectrum.
 * @note The returned struct's spectra pointer references the original div's spectra (shared, not copied).
 * @note The caller is responsible for freeing the returned struct. The xml, mz, and inten
 *       data_positions_t are newly allocated and must be freed with dealloc_dp().
 */
division_t* extract_one_spectra(division_t* div, long index) {
   data_positions_t *spectra_dp, *mz_dp, *inten_dp, *xml_dp;

   division_t* new_div = (division_t*)malloc(sizeof(division_t));
   if (new_div == NULL)
      error("extract_one_spectra: failed to allocate division_t.\n");

   xml_dp = alloc_dp(6);
   mz_dp = alloc_dp(2);
   inten_dp = alloc_dp(2);

   // Copy over xml from start to first spectra

   xml_dp->start_positions[0] = div->xml->start_positions[0];
   xml_dp->end_positions[0] = div->spectra->start_positions[0];

   new_div->size += xml_dp->end_positions[0] - xml_dp->start_positions[0];
   // mz and inten of zero for position 0
   mz_dp->start_positions[0] = 0;
   mz_dp->end_positions[0] = 0;
   inten_dp->start_positions[0] = 0;
   inten_dp->end_positions[0] = 0;

   xml_dp->start_positions[1] = 0;
   xml_dp->end_positions[1] = 0;

   // Copy over xml from index start till mz start
   xml_dp->start_positions[2] = div->spectra->start_positions[index];
   xml_dp->end_positions[2] = div->mz->start_positions[index];
   new_div->size += xml_dp->end_positions[2] - xml_dp->start_positions[2];

   // Copy over mz from index start till mz end
   mz_dp->start_positions[1] = div->mz->start_positions[index];
   mz_dp->end_positions[1] = div->mz->end_positions[index];
   new_div->size += mz_dp->end_positions[1] - mz_dp->start_positions[1];

   // Copy over xml from index start till int start
   xml_dp->start_positions[3] = div->mz->end_positions[index];
   xml_dp->end_positions[3] = div->inten->start_positions[index];
   new_div->size += xml_dp->end_positions[3] - xml_dp->start_positions[3];

   // Copy over int from index start till int end
   inten_dp->start_positions[1] = div->inten->start_positions[index];
   inten_dp->end_positions[1] = div->inten->end_positions[index];
   new_div->size += inten_dp->end_positions[1] - inten_dp->start_positions[1];

   // Copy over xml from inden end till next spectra
   xml_dp->start_positions[4] = div->inten->end_positions[index];
   xml_dp->end_positions[4] = div->spectra->start_positions[index + 1];
   new_div->size += xml_dp->end_positions[4] - xml_dp->start_positions[4];

   // Copy over xml from last spectra till end
   xml_dp->start_positions[5] =
       div->spectra->end_positions[div->mz->total_spec - 1];
   xml_dp->end_positions[5] = div->xml->end_positions[div->xml->total_spec - 1];
   new_div->size += xml_dp->end_positions[5] - xml_dp->start_positions[5];

   new_div->spectra = div->spectra;
   new_div->xml = xml_dp;
   new_div->mz = mz_dp;
   new_div->inten = inten_dp;

   return new_div;
}

/**
 * @brief Extracts N spectra from a division by index, creating a new division.
 * @param div The source division to extract from.
 * @param indicies An array of spectrum indices to extract.
 * @param n The number of indices in the array.
 * @return A newly allocated division_t containing only the selected spectra, or NULL on error.
 *
 * @note The output layout interleaves XML, m/z, and intensity positions with
 *       zero-padded entries so the downstream pipeline sees [xml, mz, xml, inten].
 * @note The caller must free the returned division.
 */
division_t* extract_n_spectra(division_t* div, long* indicies, long n)
{
   data_positions_t *spectra_dp, *mz_dp, *inten_dp, *xml_dp;

   division_t* new_div = (division_t*)malloc(sizeof(division_t));
   if (new_div == NULL)
      error("extract_one_spectra: failed to allocate division_t.\n");

   xml_dp = alloc_dp(((n * 5) + 3));
   mz_dp = alloc_dp(n * 2);
   inten_dp = alloc_dp(n * 2);

   // base case
   // Copy over xml from start to first spectra

   xml_dp->start_positions[0] = div->xml->start_positions[0];
   xml_dp->end_positions[0] = div->spectra->start_positions[0];

   new_div->size += xml_dp->end_positions[0] - xml_dp->start_positions[0];
   // mz and inten of zero for position 0
   mz_dp->start_positions[0] = 0;
   mz_dp->end_positions[0] = 0;
   inten_dp->start_positions[0] = 0;
   inten_dp->end_positions[0] = 0;

   xml_dp->start_positions[1] = 0;
   xml_dp->end_positions[1] = 0;

   long xml_curr = 2;
   long mz_curr = 1;
   long inten_curr = 1;

   for (long i = 0; i < n; i++) {
      long index = indicies[i];

      // Copy over xml from index start till mz start
      xml_dp->start_positions[xml_curr] = div->spectra->start_positions[index];
      xml_dp->end_positions[xml_curr] = div->mz->start_positions[index];
      new_div->size +=
          xml_dp->end_positions[xml_curr] - xml_dp->start_positions[xml_curr];

      xml_curr++;

      // Copy over mz from index start till mz end
      mz_dp->start_positions[mz_curr] = div->mz->start_positions[index];
      mz_dp->end_positions[mz_curr] = div->mz->end_positions[index];
      new_div->size +=
          mz_dp->end_positions[mz_curr] - mz_dp->start_positions[mz_curr];

      mz_curr++;

      // Copy over xml from index start till int start
      xml_dp->start_positions[xml_curr] = div->mz->end_positions[index];
      xml_dp->end_positions[xml_curr] = div->inten->start_positions[index];
      new_div->size +=
          xml_dp->end_positions[xml_curr] - xml_dp->start_positions[xml_curr];

      xml_curr++;

      // Copy over int from index start till int end
      inten_dp->start_positions[inten_curr] =
          div->inten->start_positions[index];
      inten_dp->end_positions[inten_curr] = div->inten->end_positions[index];
      new_div->size += inten_dp->end_positions[inten_curr] -
                       inten_dp->start_positions[inten_curr];

      inten_curr++;

      // Copy over xml from inden end till next spectra
      xml_dp->start_positions[xml_curr] = div->inten->end_positions[index];
      xml_dp->end_positions[xml_curr] =
          div->spectra->start_positions[index + 1];
      new_div->size +=
          xml_dp->end_positions[xml_curr] - xml_dp->start_positions[xml_curr];

      xml_curr++;

      // padding for mz and inten
      mz_dp->start_positions[mz_curr] = 0;
      mz_dp->end_positions[mz_curr] = 0;
      mz_curr++;
      xml_dp->start_positions[xml_curr] = 0;
      xml_dp->end_positions[xml_curr] = 0;
      xml_curr++;
      inten_dp->start_positions[inten_curr] = 0;
      inten_dp->end_positions[inten_curr] = 0;
      inten_curr++;
   }

   // end case

   // Copy over xml from last spectra till end
   xml_dp->start_positions[xml_curr] =
       div->spectra->end_positions[div->mz->total_spec - 1];
   xml_dp->end_positions[xml_curr] =
       div->xml->end_positions[div->xml->total_spec - 1];
   new_div->size +=
       xml_dp->end_positions[xml_curr] - xml_dp->start_positions[xml_curr];
   xml_curr++;

   new_div->spectra = div->spectra;
   new_div->xml = xml_dp;
   new_div->mz = mz_dp;
   new_div->inten = inten_dp;

   return new_div;
}

/**
 * @brief Computes the total encoded length across all spectra in a data_positions_t.
 * @param dp A pointer to the data_positions_t struct.
 * @return The sum of (end_position - start_position) for all spectra.
 */
long encodedLength_sum(data_positions_t* dp) {
   if (dp == NULL)
      error("encodedLength_sum: NULL pointer passed in.\n");

   int i = 0;
   long res = 0;

   for (; i < dp->total_spec; i++)
      res += dp->end_positions[i] - dp->start_positions[i];

   return res;
}

/* === End of XML traversal functions === */

data_positions_t** get_binary_divisions(data_positions_t* dp, long* blocksize,
                                        int* divisions, int* threads) {
   data_positions_t** r;
   int i = 0;
   long curr_size = 0;
   int curr_div = 0;
   int curr_div_i = 0;

   if (*divisions == 0)
      *divisions = ceil((((double)dp->file_end) / (*blocksize)));

   if (*divisions < *threads && *threads > 0) {
      *divisions = *threads;
      *blocksize = dp->file_end / (*threads);
      print("\tUsing new blocksize: %ld bytes.\n", *blocksize);
   }

   print("\tUsing %d divisions over %d threads.\n", *divisions, *threads);

   r = alloc_ddp(*divisions, (dp->total_spec * 2));

   i = 0;

   print("\t=== Divisions distribution (bytes%%/spec%%) ===\n\t");

   for (; i < dp->total_spec * 2; i++) {
      if (curr_size > (*blocksize)) {
         print("(%2.4f%%/%2.2f%%) ", (double)curr_size / dp->file_end * 100,
               (double)(r[curr_div]->total_spec) / dp->total_spec * 100);
         curr_div++;
         curr_div_i = 0;
         curr_size = 0;
      }

      if (curr_div >= *divisions) {
         fprintf(stderr,
                 "err: curr_div > divisions\ncurr_div: %d\ndivisions: "
                 "%d\ncurr_div_i: %d\ncurr_size: %d\ni: %d\ntotal_spec: %d\n",
                 curr_div, *divisions, curr_div_i, curr_size, i,
                 dp->total_spec * 2);
         exit(-1);
      }
      r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
      r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
      r[curr_div]->total_spec++;
      curr_size += (dp->end_positions[i] - dp->start_positions[i]);
      curr_div_i++;
   }

   print("(%2.4f%%/%2.2f%%) ", (double)curr_size / dp->file_end * 100,
         (double)(r[curr_div]->total_spec) / dp->total_spec * 100);
   print("\n");

   if (curr_div != *divisions)
      *divisions = curr_div + 1;

   if (*divisions < *threads) {
      *threads = *divisions;
      print("\tNEW: Using %d divisions over %d threads.\n", *divisions,
            *threads);
      *blocksize = dp->file_end / (*threads);
      print("\tUsing new blocksize: %ld bytes.\n", *blocksize);
   }

   return r;
}

data_positions_t*** new_get_binary_divisions(data_positions_t** ddp,
                                             int ddp_len, long* blocksize,
                                             int* divisions, long* threads) {
   if (ddp == NULL)
      error("new_get_binary_divisions: NULL pointer passed in.\n");
   if (ddp_len < 1)
      error("new_get_binary_divisions: ddp_len < 1.\n");
   if (*threads < 1)
      error("new_get_binary_divisions: threads < 1.\n");

   data_positions_t*** r = malloc(sizeof(data_positions_t**) * ddp_len);

   if (r == NULL)
      error("new_get_binary_divisions: malloc failed.\n");

   int i = 0, j = 0, curr_div = 0, curr_div_i = 0, curr_size = 0;

   *divisions = *threads;

   for (j = 0; j < ddp_len; j++)
      r[j] =
          alloc_ddp(*divisions, (int)ceil(ddp[0]->total_spec / (*divisions)));

   long encoded_sum = encodedLength_sum(ddp[0]);
   if (encoded_sum <= 0)
      error("new_get_binary_divisions: encoded_sum <= 0.\n");

   long bs = encoded_sum / (*divisions);

   int bound = ddp[0]->total_spec;
   if (bound <= 0)
      error("new_get_binary_divisions: bound <= 0.\n");

   for (; i < bound; i++) {
      // if(curr_div_i >= r[0][curr_div]->total_spec)
      //     error("new_get_binary_divisions: curr_div_i >=
      //     r[0][curr_div]->total_spec.\n");
      if (curr_size >= bs) {
         curr_div++;
         curr_div_i = 0;
         curr_size = 0;
         if (curr_div > *divisions)
            break;
      }
      for (j = 0; j < ddp_len; j++) {
         r[j][curr_div]->start_positions[curr_div_i] =
             ddp[j]->start_positions[i];
         r[j][curr_div]->end_positions[curr_div_i] = ddp[j]->end_positions[i];
         r[j][curr_div]->total_spec++;
      }
      curr_size += (ddp[0]->end_positions[i] - ddp[0]->start_positions[i]);
      curr_div_i++;
   }

   curr_div--;

   /* add the remainder to the last division */
   for (; i < bound; i++) {
      for (j = 0; j < ddp_len; j++) {
         r[j][curr_div]->start_positions[curr_div_i] =
             ddp[j]->start_positions[i];
         r[j][curr_div]->end_positions[curr_div_i] = ddp[j]->end_positions[i];
         r[j][curr_div]->total_spec++;
      }
   }

   return r;
}

/**
 * @brief Splits XML data positions into evenly-sized divisions.
 * @param dp The source data positions containing all XML spectra.
 * @param divisions The number of divisions to create.
 * @return A newly allocated array of data_positions_t pointers, one per division.
 * @note The caller must free the returned array via free_ddp().
 */
data_positions_t** new_get_xml_divisions(data_positions_t* dp, int divisions) {
   data_positions_t** r;

   int i = 0, curr_div = 0, curr_div_i = 0, curr_size = 0;

   int bound = dp->total_spec,
       div_bound = (int)ceil(dp->total_spec / divisions);

   r = alloc_ddp(divisions,
                 div_bound + divisions);  // allocate extra room for remainders

   // check if r is null
   if (r == NULL) {
      fprintf(stderr, "err: r is null\n");
      exit(-1);
   }

   for (; i <= bound; i++) {
      if (curr_div_i > div_bound) {
         r[curr_div]->file_end = dp->file_end;
         curr_div++;
         curr_div_i = 0;
         curr_size = 0;
         if (curr_div == divisions)
            break;
      }

      r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
      r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
      r[curr_div]->total_spec++;
      curr_div_i++;
   }

   curr_div--;

   // put remainder in last division
   for (; i <= bound; i++) {
      r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
      r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
      r[curr_div]->total_spec++;
   }

   return r;
}

/**
 * @brief Splits XML data positions into divisions aligned with binary division boundaries.
 * @param dp The source data positions containing all XML spectra.
 * @param binary_divisions The binary division boundaries to align with.
 * @param divisions The number of divisions.
 * @return A newly allocated array of data_positions_t pointers, one per division.
 * @note The caller must free the returned array via free_ddp().
 */
data_positions_t** get_xml_divisions(data_positions_t* dp,
                                     data_positions_t** binary_divisions,
                                     int divisions) {
   data_positions_t** r;

   int i;
   int curr_div = 0;
   int curr_div_i = 0;
   int curr_bin_i = 0;

   r = alloc_ddp(divisions, dp->total_spec);

   /* base case */

   r[curr_div]->start_positions[curr_div_i] = 0;
   r[curr_div]->end_positions[curr_div_i] =
       binary_divisions[0]->start_positions[0];
   r[curr_div]->total_spec++;
   r[curr_div]->file_end = dp->file_end;
   curr_div_i++;
   curr_bin_i++;

   /* inductive step */

   i = 0;

   while (i < dp->total_spec * 2) {
      if (curr_bin_i > binary_divisions[curr_div]->total_spec - 1) {
         if (curr_div + 1 == divisions)
            break;
         r[curr_div]->file_end = dp->file_end;
         r[curr_div]->end_positions[curr_div_i - 1] =
             binary_divisions[curr_div + 1]->start_positions[0];
         curr_div++;

         /* First xml division of 0 length, start binary first */
         r[curr_div]->start_positions[0] =
             binary_divisions[curr_div]->end_positions[0];
         r[curr_div]->end_positions[0] =
             binary_divisions[curr_div]->end_positions[0];
         r[curr_div]->total_spec++;

         r[curr_div]->start_positions[1] =
             binary_divisions[curr_div]->end_positions[0];
         r[curr_div]->end_positions[1] =
             binary_divisions[curr_div]->start_positions[1];
         r[curr_div]->total_spec++;
         curr_div_i = 2;
         curr_bin_i = 2;
      } else {
         r[curr_div]->start_positions[curr_div_i] =
             binary_divisions[curr_div]->end_positions[curr_bin_i - 1];
         r[curr_div]->end_positions[curr_div_i] =
             binary_divisions[curr_div]->start_positions[curr_bin_i];
         r[curr_div]->total_spec++;
         curr_div_i++;
         curr_bin_i++;
         i++;
      }
   }

   /* end case */
   r[curr_div]->start_positions[curr_div_i] =
       binary_divisions[curr_div]->end_positions[curr_bin_i - 1];
   r[curr_div]->end_positions[curr_div_i] = dp->file_end;
   r[curr_div]->total_spec++;
   r[curr_div]->file_end = dp->file_end;

   return r;
}

/**
 * @brief Serializes a data_positions_t struct to a file descriptor.
 * @param dp Pointer to the data_positions_t to serialize.
 * @param fd The file descriptor to write to.
 *
 * Writes total_spec count followed by the start and end position arrays.
 */
void write_dp(data_positions_t* dp, int fd) {
   char *buff, *num_buff;

   // Write total_spec (spectrum count)
   num_buff = malloc(sizeof(uint64_t));
   *((uint64_t*)num_buff) = dp->total_spec;
   write_to_file(fd, num_buff, sizeof(uint64_t));

   // Write start positions
   buff = (char*)dp->start_positions;
   write_to_file(fd, buff, sizeof(uint64_t) * dp->total_spec);

   // Write end positions
   buff = (char*)dp->end_positions;
   write_to_file(fd, buff, sizeof(uint64_t) * dp->total_spec);

   free(num_buff);

   return;
}

/**
 * @brief Writes a uint32_t array prefixed by its length to a file descriptor.
 * @param arr Pointer to the uint32_t array.
 * @param len Number of elements in the array.
 * @param fd The file descriptor to write to.
 */
void write_uint32_arr(uint32_t* arr, uint32_t len, int fd) {
   char *buff, *num_buff;

   // Write array length
   num_buff = malloc(sizeof(uint32_t));
   *((uint32_t*)num_buff) = len;
   write_to_file(fd, num_buff, sizeof(uint32_t));

   // Write array
   buff = (char*)arr;
   write_to_file(fd, buff, sizeof(uint32_t) * len);

   free(num_buff);

   return;
}

/**
 * @brief Writes a uint16_t array prefixed by its length to a file descriptor.
 * @param arr Pointer to the uint16_t array.
 * @param len Number of elements in the array.
 * @param fd The file descriptor to write to.
 */
void write_uint16_arr(uint16_t* arr, uint32_t len, int fd) {
   char *buff, *num_buff;

   // Write array length
   num_buff = malloc(sizeof(uint32_t));
   *((uint32_t*)num_buff) = len;
   write_to_file(fd, num_buff, sizeof(uint32_t));

   // Write array
   buff = (char*)arr;
   write_to_file(fd, buff, sizeof(uint16_t) * len);

   free(num_buff);

   return;
}

/**
 * @brief Deserializes a data_positions_t struct from a memory-mapped buffer.
 * @param input_map Pointer to the memory-mapped input file.
 * @param position Pointer to the current read offset; advanced past the read data on return.
 * @return Pointer to a newly allocated data_positions_t, or NULL on allocation failure.
 *
 * @note The returned struct's start_positions and end_positions point directly into the
 *       mmap'd region and must not be freed independently.
 * @warning The caller must free the returned struct wrapper with free().
 */
data_positions_t* read_dp(void* input_map, long* position) {
   data_positions_t* r = malloc(sizeof(data_positions_t));
   if (r == NULL)
      return NULL;

   // Read total_spec
   r->total_spec = *((uint64_t*)((uint8_t*)input_map + *position));
   *position += sizeof(uint64_t);

   // Read start positions
   r->start_positions = (uint64_t*)((uint8_t*)input_map + *position);
   *position += sizeof(uint64_t) * r->total_spec;

   // Read end positions
   r->end_positions = (uint64_t*)((uint8_t*)input_map + *position);
   *position += sizeof(uint64_t) * r->total_spec;

   return r;
}

/**
 * @brief Reads a length-prefixed uint32_t array from a memory-mapped buffer.
 * @param input_map Pointer to the memory-mapped input file.
 * @param position Pointer to the current read offset; advanced past the read data on return.
 * @return Pointer into the mmap'd region containing the array data.
 *
 * @note The returned pointer references mmap'd memory and must not be freed.
 */
uint32_t* read_uint32_arr(void* input_map, long* position) {
   uint32_t len = *((uint32_t*)((uint8_t*)input_map + *position));
   *position += sizeof(uint32_t);

   uint32_t* arr = (uint32_t*)((uint8_t*)input_map + *position);
   *position += sizeof(uint32_t) * len;

   return arr;
}

/**
 * @brief Reads a length-prefixed uint16_t array from a memory-mapped buffer.
 * @param input_map Pointer to the memory-mapped input file.
 * @param position Pointer to the current read offset; advanced past the read data on return.
 * @return Pointer into the mmap'd region containing the array data.
 *
 * @note The returned pointer references mmap'd memory and must not be freed.
 */
uint16_t* read_uint16_arr(void* input_map, long* position) {
   uint32_t len = *((uint32_t*)((uint8_t*)input_map + *position));
   *position += sizeof(uint32_t);

   uint16_t* arr = (uint16_t*)((uint8_t*)input_map + *position);
   *position += sizeof(uint16_t) * len;

   return arr;
}

/**
 * @brief Serializes a single division_t struct to a file descriptor.
 * @param div Pointer to the division_t to serialize.
 * @param fd The file descriptor to write to.
 *
 * Writes all four data_positions_t (spectra, xml, mz, inten), the division size,
 * and the scan/ms_level arrays.
 */
void write_division(division_t* div, int fd) {
   char *buff, *num_buff;

   // Write data_positions_t
   write_dp(div->spectra, fd);
   write_dp(div->xml, fd);
   write_dp(div->mz, fd);
   write_dp(div->inten, fd);

   // Write size of division
   num_buff = malloc(sizeof(uint64_t));
   *((uint64_t*)num_buff) = (uint64_t)div->size;
   write_to_file(fd, num_buff, sizeof(uint64_t));
   free(num_buff);

   // Write scans and MS levels
   write_uint32_arr(div->scans, div->mz->total_spec, fd);
   write_uint16_arr(div->ms_levels, div->mz->total_spec, fd);

   return;
}

/**
 * @brief Serializes all divisions in a divisions_t struct to a file descriptor.
 * @param divisions Pointer to the divisions_t containing the array of divisions.
 * @param fd The file descriptor to write to.
 */
void write_divisions(divisions_t* divisions, int fd) {
   for (int i = 0; i < divisions->n_divisions; i++)
      write_division(divisions->divisions[i], fd);

   return;
}

/**
 * @brief Deserializes a single division_t from a memory-mapped buffer.
 * @param input_map Pointer to the memory-mapped input file.
 * @param position Pointer to the current read offset; advanced past the read data on return.
 * @return Pointer to a newly allocated division_t, or NULL on allocation failure.
 *
 * @note The internal arrays (scans, ms_levels, position arrays) point into the mmap'd region.
 * @warning Free with dealloc_read_division() to avoid freeing mmap'd data.
 */
division_t* read_division(void* input_map, long* position) {
   division_t* r;

   r = malloc(sizeof(division_t));
   if (r == NULL)
      return NULL;

   r->spectra = read_dp(input_map, position);
   r->xml = read_dp(input_map, position);
   r->mz = read_dp(input_map, position);
   r->inten = read_dp(input_map, position);
   r->size = *((uint64_t*)((uint8_t*)input_map + *position));
   *position += sizeof(uint64_t);

   r->scans = read_uint32_arr(input_map, position);
   r->ms_levels = read_uint16_arr(input_map, position);

   r->ret_times = NULL;

   return r;
}

/**
 * @brief Deserializes all divisions from a memory-mapped buffer.
 * @param input_map Pointer to the memory-mapped input file.
 * @param position The byte offset where division data begins.
 * @param n_divisions Number of divisions to read.
 * @return Pointer to a newly allocated divisions_t, or NULL on allocation failure.
 *
 * @warning Free with dealloc_read_divisions() to avoid freeing mmap'd data.
 */
divisions_t* read_divisions(void* input_map, long position, int n_divisions) {
   divisions_t* r;

   r = malloc(sizeof(divisions_t));
   if (r == NULL)
      return NULL;
   r->divisions = malloc(sizeof(division_t*) * n_divisions);
   if (r->divisions == NULL)
      return NULL;

   for (int i = 0; i < n_divisions; i++)
      r->divisions[i] = read_division(input_map, &position);

   r->n_divisions = n_divisions;

   return r;
}

/**
 * @brief Merges all divisions into a single flat division_t.
 * @param divisions Pointer to the divisions_t containing multiple divisions.
 * @return Pointer to a newly allocated division_t containing all spectra, or NULL on error.
 *
 * Concatenates all spectra, xml, mz, inten positions, scans, and ms_levels from
 * every division into one contiguous division.
 *
 * @warning The caller must free the returned division with dealloc_division().
 */
division_t* flatten_divisions(divisions_t* divisions) {
   size_t spectra_size = 0;
   size_t xml_size = 0;
   size_t mz_size = 0;
   size_t inten_size = 0;
   size_t total_size = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      spectra_size += divisions->divisions[i]->spectra->total_spec;
      xml_size += divisions->divisions[i]->xml->total_spec;
      mz_size += divisions->divisions[i]->mz->total_spec;
      inten_size += divisions->divisions[i]->inten->total_spec;
      total_size += divisions->divisions[i]->size;
   }

   division_t* r = alloc_division(xml_size, mz_size, inten_size);

   size_t index = 0;
   for (int i = 0; i < divisions->n_divisions; i++) {
      for (int j = 0; j < divisions->divisions[i]->spectra->total_spec; j++) {
         r->spectra->start_positions[index] =
             divisions->divisions[i]->spectra->start_positions[j];
         r->spectra->end_positions[index] =
             divisions->divisions[i]->spectra->end_positions[j];
         index++;
      }
   }
   r->spectra->total_spec = spectra_size;
   index = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      for (int j = 0; j < divisions->divisions[i]->xml->total_spec; j++) {
         r->xml->start_positions[index] =
             divisions->divisions[i]->xml->start_positions[j];
         r->xml->end_positions[index] =
             divisions->divisions[i]->xml->end_positions[j];
         index++;
      }
   }
   r->xml->total_spec = xml_size;
   index = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      for (int j = 0; j < divisions->divisions[i]->mz->total_spec; j++) {
         r->mz->start_positions[index] =
             divisions->divisions[i]->mz->start_positions[j];
         r->mz->end_positions[index] =
             divisions->divisions[i]->mz->end_positions[j];
         index++;
      }
   }
   r->mz->total_spec = mz_size;
   index = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      for (int j = 0; j < divisions->divisions[i]->inten->total_spec; j++) {
         r->inten->start_positions[index] =
             divisions->divisions[i]->inten->start_positions[j];
         r->inten->end_positions[index] =
             divisions->divisions[i]->inten->end_positions[j];
         index++;
      }
   }
   r->inten->total_spec = inten_size;
   index = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      for (int j = 0; j < divisions->divisions[i]->mz->total_spec; j++) {
         r->scans[index] = divisions->divisions[i]->scans[j];
         r->ms_levels[index] = divisions->divisions[i]->ms_levels[j];
         index++;
      }
   }
   r->size = total_size;

   return r;
}

/**
 * @brief Extracts an array of XML data_positions_t pointers from all divisions.
 * @param divisions Pointer to the divisions_t.
 * @return Array of data_positions_t pointers (one per division), or NULL on allocation failure.
 *
 * @warning The caller must free the returned array (but not the individual pointers,
 *          which are owned by the divisions).
 */
data_positions_t** join_xml(divisions_t* divisions) {
   data_positions_t** r;
   r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
   if (r == NULL)
      return NULL;
   for (int i = 0; i < divisions->n_divisions; i++)
      r[i] = divisions->divisions[i]->xml;
   return r;
}

/**
 * @brief Extracts an array of m/z data_positions_t pointers from all divisions.
 * @param divisions Pointer to the divisions_t.
 * @return Array of data_positions_t pointers (one per division), or NULL on allocation failure.
 *
 * @warning The caller must free the returned array (but not the individual pointers,
 *          which are owned by the divisions).
 */
data_positions_t** join_mz(divisions_t* divisions) {
   data_positions_t** r;
   r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
   if (r == NULL)
      return NULL;
   for (int i = 0; i < divisions->n_divisions; i++)
      r[i] = divisions->divisions[i]->mz;
   return r;
}

/**
 * @brief Extracts an array of intensity data_positions_t pointers from all divisions.
 * @param divisions Pointer to the divisions_t.
 * @return Array of data_positions_t pointers (one per division), or NULL on allocation failure.
 *
 * @warning The caller must free the returned array (but not the individual pointers,
 *          which are owned by the divisions).
 */
data_positions_t** join_inten(divisions_t* divisions) {
   data_positions_t** r;
   r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
   if (r == NULL)
      return NULL;
   for (int i = 0; i < divisions->n_divisions; i++)
      r[i] = divisions->divisions[i]->inten;
   return r;
}

/**
 * @brief Splits a single division into multiple divisions for parallel processing.
 * @param div Pointer to the source division_t containing all spectra.
 * @param n_divisions Number of divisions to create (actual count will be n_divisions + 1
 *                    to hold any remaining XML).
 * @return Pointer to a newly allocated divisions_t, or NULL on allocation failure.
 *
 * Distributes spectra evenly across n_divisions, with leftover spectra assigned to the
 * last division. An extra division is appended for any trailing XML data.
 *
 * @warning The caller must free with dealloc_divisions().
 */
divisions_t* create_divisions(division_t* div, long n_divisions) {
   divisions_t* r;

   r = malloc(sizeof(divisions_t));
   if (r == NULL)
      return NULL;

   r->divisions = malloc(sizeof(division_t*) * (n_divisions + 1));
   if (r->divisions == NULL)
      return NULL;

   // r->n_divisions = n_threads;
   r->n_divisions = n_divisions + 1;  // n_divisions + 1 for the last division
                                      // containing only remaining XML.

   //  Determine roughly how many spectra each division will contain
   long n_spec_per_div = div->mz->total_spec / n_divisions;

   // Determine how many spectra will be left over
   long n_spec_leftover = div->mz->total_spec % n_divisions;

   int spec_i = 0;
   int xml_i = 0;
   for (int i = 0; i < n_divisions - 1; i++) {
      r->divisions[i] =
          alloc_division(n_spec_per_div * 2, n_spec_per_div, n_spec_per_div);

      for (int j = 0; j < n_spec_per_div; j++) {
         // Copy Spectra
         r->divisions[i]->spectra->start_positions[j] =
             div->spectra->start_positions[spec_i];
         r->divisions[i]->spectra->end_positions[j] =
             div->spectra->end_positions[spec_i];
         r->divisions[i]->spectra->total_spec++;

         // Copy MZ
         r->divisions[i]->mz->start_positions[j] =
             div->mz->start_positions[spec_i];
         r->divisions[i]->mz->end_positions[j] = div->mz->end_positions[spec_i];
         r->divisions[i]->mz->total_spec++;
         r->divisions[i]->size +=
             div->mz->end_positions[spec_i] - div->mz->start_positions[spec_i];

         // Copy Inten
         r->divisions[i]->inten->start_positions[j] =
             div->inten->start_positions[spec_i];
         r->divisions[i]->inten->end_positions[j] =
             div->inten->end_positions[spec_i];
         r->divisions[i]->inten->total_spec++;
         r->divisions[i]->size += div->inten->end_positions[spec_i] -
                                  div->inten->start_positions[spec_i];

         // Copy scans, MS levels, etc.
         r->divisions[i]->scans[j] = div->scans[spec_i];
         r->divisions[i]->ms_levels[j] = div->ms_levels[spec_i];

         spec_i++;
      }

      for (int j = 0; j < n_spec_per_div * 2; j++) {
         // Copy XML
         r->divisions[i]->xml->start_positions[j] =
             div->xml->start_positions[xml_i];
         r->divisions[i]->xml->end_positions[j] =
             div->xml->end_positions[xml_i];
         r->divisions[i]->xml->total_spec++;
         r->divisions[i]->size +=
             div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];
         xml_i++;
      }
   }

   // End case: take the remaining spectra and put them in the last division
   // TODO: factor this out into a function
   int i = n_divisions - 1;
   n_spec_per_div += n_spec_leftover;

   r->divisions[i] =
       alloc_division(n_spec_per_div * 2, n_spec_per_div, n_spec_per_div);

   for (int j = 0; j < n_spec_per_div; j++) {
      // Copy Spectra
      r->divisions[i]->spectra->start_positions[j] =
          div->spectra->start_positions[spec_i];
      r->divisions[i]->spectra->end_positions[j] =
          div->spectra->end_positions[spec_i];
      r->divisions[i]->spectra->total_spec++;

      // Copy MZ
      r->divisions[i]->mz->start_positions[j] =
          div->mz->start_positions[spec_i];
      r->divisions[i]->mz->end_positions[j] = div->mz->end_positions[spec_i];
      r->divisions[i]->mz->total_spec++;
      r->divisions[i]->size +=
          div->mz->end_positions[spec_i] - div->mz->start_positions[spec_i];

      // Copy Inten
      r->divisions[i]->inten->start_positions[j] =
          div->inten->start_positions[spec_i];
      r->divisions[i]->inten->end_positions[j] =
          div->inten->end_positions[spec_i];
      r->divisions[i]->inten->total_spec++;
      r->divisions[i]->size += div->inten->end_positions[spec_i] -
                               div->inten->start_positions[spec_i];

      // Copy scans, MS levels, etc.
      r->divisions[i]->scans[j] = div->scans[spec_i];
      r->divisions[i]->ms_levels[j] = div->ms_levels[spec_i];

      spec_i++;
   }

   for (int j = 0; j < n_spec_per_div * 2; j++) {
      // Copy XML
      r->divisions[i]->xml->start_positions[j] =
          div->xml->start_positions[xml_i];
      r->divisions[i]->xml->end_positions[j] = div->xml->end_positions[xml_i];
      r->divisions[i]->xml->total_spec++;
      r->divisions[i]->size +=
          div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];
      xml_i++;
   }

   // End case: remaining XML
   int remaining_xml = div->xml->total_spec - xml_i;
   assert(remaining_xml >= 0);
   if (remaining_xml == 0)
      return r;

   r->divisions[n_divisions] = alloc_division(remaining_xml, 0, 0);
   for (int j = 0; j < remaining_xml; j++) {
      r->divisions[n_divisions]->xml->start_positions[j] =
          div->xml->start_positions[xml_i];
      r->divisions[n_divisions]->xml->end_positions[j] =
          div->xml->end_positions[xml_i];
      r->divisions[n_divisions]->xml->total_spec++;
      r->divisions[n_divisions]->size +=
          div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];
      xml_i++;
   }

   return r;
}

/**
 * @brief Calculates the number of divisions based on file size and block size.
 * @param filesize Total size of the input file in bytes.
 * @param blocksize Target block size per division in bytes.
 * @return The number of divisions (minimum 1).
 */
long determine_n_divisions(long filesize, long blocksize) {
   if (blocksize == 0)
      error("Blocksize cannot be 0.\n");
   if (filesize == 0)
      error("Filesize cannot be 0.\n");

   long n_divisions = filesize / blocksize;

   if (n_divisions == 0)
      n_divisions = 1;

   return n_divisions;
}

/**
 * @brief Returns the maximum size among all divisions.
 * @param divisions Pointer to the divisions_t to search.
 * @return The largest division size in bytes.
 */
long get_division_size_max(divisions_t* divisions) {
   long max = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      if (divisions->divisions[i]->size > max)
         max = divisions->divisions[i]->size;
   }

   return max;
}

/**
 * @brief Checks whether an array of longs is strictly monotonically increasing.
 * @param arr Pointer to the array of longs.
 * @param size Number of elements in the array.
 * @return 1 if the array is strictly increasing, 0 otherwise.
 */
int is_monotonically_increasing(long* arr, long size) {
   long i;
   for (i = 0; i < size - 1; i++) {
      if (arr[i] >= arr[i + 1]) {
         return 0;
      }
   }
   return 1;
}

/**
 * @brief Validates that a string contains only valid scan-range characters.
 * @param str The input string to validate.
 * @return 1 if valid (digits, dashes, brackets, commas, whitespace only), 0 otherwise.
 */
int is_valid_input(char* str) {
   int len = strlen(str);
   int i;

   for (i = 0; i < len; i++) {
      // Allow digits, dash (for ranges), brackets, comma (for separating
      // ranges), and whitespace Brackets are now optional
      if (!(str[i] >= '0' && str[i] <= '9') && str[i] != '-' && str[i] != '[' &&
          str[i] != ']' && str[i] != ',' && !isspace((unsigned char)str[i])) {
         return 0;
      }
   }

   return 1;
}

/**
 * @brief Parses a scan-range string into an expanded array of indices.
 * @param str The input string (e.g., "[0-100,200-300]" or "0-100,200-300").
 * @param size Pointer set to the number of elements in the returned array.
 * @return Pointer to a malloc'd array of longs containing the expanded indices.
 *
 * Supports both bracketed and non-bracketed formats. Ranges are expanded
 * inclusively (e.g., "1-3" becomes {1, 2, 3}).
 *
 * @note The resulting array must be monotonically increasing; an error is raised if not.
 * @warning The caller must free the returned array.
 */
long* string_to_array(char* str, long* size) {
   if (!is_valid_input(str))
      error("Invalid input.\n");

   int max = 1000000;  // No more than 1,000,000 spectra
   int i, j;
   int len = strlen(str);
   long* arr = malloc(max * sizeof(long));
   *size = 0;

   i = 0;

   // Skip leading whitespace
   while (i < len && isspace((unsigned char)str[i])) {
      i++;
   }

   // Skip opening bracket if present
   if (i < len && str[i] == '[') {
      i++;
   }

   while (i < len) {
      // Skip whitespace
      while (i < len && isspace((unsigned char)str[i])) {
         i++;
      }

      // Stop if we hit closing bracket or end of string
      if (i >= len || str[i] == ']') {
         break;
      }

      // Skip commas
      if (str[i] == ',') {
         i++;
         continue;
      }

      // Parse a number or range
      if (str[i] >= '0' && str[i] <= '9') {
         long start = 0;
         for (j = i; j < len && str[j] >= '0' && str[j] <= '9'; j++) {
            start = start * 10 + (str[j] - '0');
         }
         i = j;

         long end = start;  // Initialize end to start

         // Check for range (dash followed by a number)
         if (i < len && str[i] == '-') {
            // Look ahead to distinguish between minus sign and range separator
            // If next char is a digit, it's a range
            if (i + 1 < len && str[i + 1] >= '0' && str[i + 1] <= '9') {
               i++;  // Skip the dash
               end = 0;
               for (j = i; j < len && str[j] >= '0' && str[j] <= '9'; j++) {
                  end = end * 10 + (str[j] - '0');
               }
               i = j;
            }
         }

         // Add all numbers in the range [start, end]
         for (long num = start; num <= end; num++) {
            if (*size >= max)
               error("Too many spectra specified.\n");
            arr[*size] = num;
            (*size)++;
         }
      } else {
         i++;
      }
   }

   if (!is_monotonically_increasing(arr, *size))
      error("Scans must be monotonically increasing.\n");

   return arr;
}

/**
 * @brief Maps an array of scan numbers to their corresponding spectrum indices within a division.
 * @param scans Array of scan numbers to look up.
 * @param scans_length Number of scans in the array.
 * @param div Pointer to the division_t to search.
 * @param index_offset Offset added to each matched index (for multi-division indexing).
 * @param indices_length Pointer set to the number of matched indices on return.
 * @return Pointer to a malloc'd array of matched indices, or NULL if not monotonically increasing.
 *
 * @note Scans not found in the division are silently skipped.
 * @warning The caller must free the returned array.
 */
long* map_scan_to_index(uint32_t* scans, long scans_length, division_t* div,
                        long index_offset, long* indices_length) {
   long i, j, k;
   long* indicies = malloc(scans_length * sizeof(long));

   j = 0;

   for (long i = 0; i < scans_length; i++) {
      for (k = 0; k < div->spectra->total_spec; k++) {
         if (scans[i] == div->scans[k]) {
            indicies[j] = k + index_offset;
            j++;
            break;
         }
      }
      // if(k == div->spectra->total_spec)
      //     error("Scan %ld not found in file.\n", scans[i]);
   }

   if (!is_monotonically_increasing(indicies, j)) {
      warning("map_scan_to_index: Scans must be monotonically increasing.\n");
      *indices_length = 0;
      return NULL;
   }

   *indices_length = j;
   return indicies;
}

/**
 * @brief Maps an MS level to spectrum indices within a division.
 * @param ms_level The MS level to filter by, or -1 to select MS levels > 2.
 * @param div Pointer to the division_t to search.
 * @param index_offset Offset added to each matched index (for multi-division indexing).
 * @param indices_length Pointer set to the number of matched indices on return.
 * @return Pointer to a malloc'd array of matched indices.
 *
 * @warning The caller must free the returned array.
 */
long* map_ms_level_to_index(uint16_t ms_level, division_t* div,
                            long index_offset, long* indices_length) {
   if (div->spectra->total_spec == 0)
      return NULL;

   long* indicies = malloc(div->spectra->total_spec * sizeof(long));

   long j = 0;

   for (long i = 0; i < div->spectra->total_spec; i++) {
      if (ms_level == -1) {
         if (div->ms_levels[i] > 2) {
            indicies[j] = i + index_offset;
            j++;
         }
      } else if (div->ms_levels[i] == ms_level) {
         indicies[j] = i + index_offset;
         j++;
      }
   }

   if (!is_monotonically_increasing(indicies, j))
      error("map_ms_level_to_index: Scans must be monotonically increasing.\n");

   *indices_length = j;

   return indicies;
}

/**
 * @brief Maps an MS level to spectrum indices across all divisions.
 * @param ms_level The MS level to filter by, or -1 to select MS levels > 2.
 * @param divisions Pointer to the divisions_t containing all divisions.
 * @param indicies_length Pointer set to the total number of matched indices on return.
 * @return Pointer to a malloc'd merged array of indices, or NULL if not monotonically increasing.
 *
 * Queries each division via map_ms_level_to_index() and merges the results into
 * a single contiguous array with correct global offsets.
 *
 * @warning The caller must free the returned array.
 */
long* map_ms_level_to_index_from_divisions(uint16_t ms_level,
                                           divisions_t* divisions,
                                           long* indicies_length) {
   long** indicies = malloc(sizeof(long*) * divisions->n_divisions);
   long* indicies_lens = calloc(divisions->n_divisions, sizeof(long));

   long total_len = 0;
   long index_offset = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      long curr_len = 0;
      indicies[i] = map_ms_level_to_index(ms_level, divisions->divisions[i],
                                          index_offset, &curr_len);
      indicies_lens[i] = curr_len;
      total_len += curr_len;
      index_offset += divisions->divisions[i]->spectra->total_spec;
   }

   long* result = malloc(sizeof(long) * total_len);

   long index = 0;
   for (int i = 0; i < divisions->n_divisions; i++) {
      if (indicies[i] == NULL)
         continue;

      for (int j = 0; j < indicies_lens[i]; j++) {
         result[index] = indicies[i][j];
         index++;
      }
   }
   // Free intermediate arrays
   for (int i = 0; i < divisions->n_divisions; i++) {
      free(indicies[i]);
   }
   free(indicies);
   free(indicies_lens);

   if (!is_monotonically_increasing(result, total_len)) {
      warning(
          "map_ms_level_to_index_from_divisions: Scans must be monotonically "
          "increasing.\n");
      *indicies_length = 0;
      free(result);
      return NULL;
   }

   *indicies_length = total_len;

   return result;
}

/**
 * @brief Maps scan numbers to spectrum indices across all divisions.
 * @param scans Array of scan numbers to look up.
 * @param scans_length Number of scans in the array.
 * @param divisions Pointer to the divisions_t containing all divisions.
 * @param indicies_length Pointer set to the total number of matched indices on return.
 * @return Pointer to a malloc'd merged array of indices, or NULL if not monotonically increasing.
 *
 * Queries each division via map_scan_to_index() and merges the results into
 * a single contiguous array with correct global offsets.
 *
 * @warning The caller must free the returned array.
 */
long* map_scans_to_index_from_divisions(uint32_t* scans, long scans_length,
                                        divisions_t* divisions,
                                        long* indicies_length) {
   long** indicies = malloc(sizeof(long*) * divisions->n_divisions);
   long* indicies_lens = calloc(divisions->n_divisions, sizeof(long));

   long total_len = 0;
   long index_offset = 0;

   for (int i = 0; i < divisions->n_divisions; i++) {
      long curr_len = 0;
      indicies[i] =
          map_scan_to_index(scans, scans_length, divisions->divisions[i],
                            index_offset, &curr_len);
      indicies_lens[i] = curr_len;
      total_len += curr_len;
      index_offset += divisions->divisions[i]->spectra->total_spec;
   }

   long* result = malloc(sizeof(long) * total_len);

   long index = 0;
   for (int i = 0; i < divisions->n_divisions; i++) {
      if (indicies[i] == NULL)
         continue;

      for (int j = 0; j < indicies_lens[i]; j++) {
         result[index] = indicies[i][j];
         index++;
      }
   }

   // Free intermediate arrays
   for (int i = 0; i < divisions->n_divisions; i++) {
      free(indicies[i]);
   }
   free(indicies);
   free(indicies_lens);

   if (!is_monotonically_increasing(result, total_len)) {
      warning(
          "map_scans_to_index_from_divisions: Scans must be monotonically "
          "increasing.\n");
      *indicies_length = 0;
      free(result);
      return NULL;
   }

   *indicies_length = total_len;
   return result;
}

/**
 * @brief Preprocesses an mzML file: detects format, scans spectra, and creates divisions.
 * @param input_map Pointer to the memory-mapped input mzML file.
 * @param input_filesize Size of the input file in bytes.
 * @param blocksize Pointer to the target block size; may be updated based on division layout.
 * @param arguments Pointer to the Arguments struct with user-specified filters
 *                  (indices, scans, ms_level, threads).
 * @param df Output pointer set to the detected data_format_t.
 * @param divisions Output pointer set to the created divisions_t.
 * @return 0 on success, 1 on error.
 *
 * Handles scan/index/ms-level filtering, determines the number of divisions,
 * and distributes spectra across divisions for parallel compression.
 */
int preprocess_mzml(char* input_map, long input_filesize, long* blocksize,
                    Arguments* arguments, data_format_t** df,
                    divisions_t** divisions) {
   double start, end;

   start = get_time();

   print("\nPreprocessing...\n");

   *df = pattern_detect((char*)input_map);

   if (*df == NULL)
      return 1;

   division_t* div = NULL;
   if (arguments->indices_length > 0) {
      division_t* tmp = scan_mzml(
          (char*)input_map, *df, input_filesize,
          MSLEVEL | SCANNUM);  // A division encapsulating the entire file
      if (tmp == NULL)
         return 1;
      div =
          extract_n_spectra(tmp, arguments->indices, arguments->indices_length);
   } else if (arguments->scans_length > 0) {
      division_t* tmp = scan_mzml(
          (char*)input_map, *df, input_filesize,
          MSLEVEL | SCANNUM);  // A division encapsulating the entire file
      if (tmp == NULL)
         return 1;
      arguments->indices =
          map_scan_to_index(arguments->scans, arguments->scans_length, tmp, 0,
                            &(arguments->indices_length));
      div =
          extract_n_spectra(tmp, arguments->indices, arguments->indices_length);

   } else if (arguments->ms_level > 0 || arguments->ms_level == -1) {
      division_t* tmp = scan_mzml(
          (char*)input_map, *df, input_filesize,
          MSLEVEL | SCANNUM);  // A division encapsulating the entire file
      if (tmp == NULL)
         return 1;
      arguments->indices = map_ms_level_to_index(arguments->ms_level, tmp, 0,
                                                 &(arguments->indices_length));
      div =
          extract_n_spectra(tmp, arguments->indices, arguments->indices_length);
   } else if (arguments->indices_length == 0 && arguments->scans_length == 0) {
      div = scan_mzml(
          (char*)input_map, *df, input_filesize,
          MSLEVEL | SCANNUM);  // A division encapsulating the entire file
   } else
      error("Invalid indicies_size: %ld\n", arguments->indices_length);

   if (div == NULL)
      return 1;

   if (arguments->threads == -1)  // force divisions to be only 1
   {
      arguments->threads = 1;
      *blocksize = div->size;
      *divisions = (divisions_t*)malloc(sizeof(divisions_t));
      (*divisions)->divisions = (division_t**)malloc(sizeof(division_t*));
      (*divisions)->divisions[0] = div;
      (*divisions)->n_divisions = 1;
   } else {
      long n_divisions = determine_n_divisions(div->size, *blocksize);

      if (n_divisions >
          div->mz->total_spec)  // If we have more divisions than spectra, we
                                // need to decrease the number of divisions
      {
         print(
             "Warning: n_divisions (%ld) > total_spec (%ld). Setting "
             "n_divisions to total_spec)\n",
             n_divisions, div->mz->total_spec);
         n_divisions = div->mz->total_spec;
      }

      if (arguments->indices_length != 0 &&
          arguments->threads >
              arguments->indices_length)  // If we have more threads than
                                          // selected specta, we need to
                                          // decrease the number of divisions
      {
         print(
             "Warning: n_threads (%ld) > indices_length (%ld). Setting "
             "n_divisions to indices_length)\n",
             arguments->threads, arguments->indices_length);
         n_divisions = arguments->indices_length;
         *divisions = create_divisions(div, n_divisions);
      } else if (n_divisions >=
                 arguments->threads)  // Create divisions. Either n_divisions or
                                      // n_threads, whichever is greater
         *divisions = create_divisions(div, n_divisions);
      else {
         long effective_divisions = arguments->threads;
         if (effective_divisions > div->mz->total_spec) {
            print(
                "Warning: n_threads (%ld) > total_spec (%ld). Setting "
                "n_divisions to total_spec)\n",
                effective_divisions, div->mz->total_spec);
            effective_divisions = div->mz->total_spec;
         }
         *divisions = create_divisions(div, effective_divisions);
         *blocksize = get_division_size_max(
             *divisions);  // If we have more threads than divisions, we need to
                           // increase the blocksize to the max division size
      }
   }

   if (*divisions == NULL)
      return 1;

   end = get_time();

   print("Preprocessing time: %1.4fs\n", end - start);
   print("Using %ld divisions over %ld threads.\n", (*divisions)->n_divisions,
         arguments->threads);

   return 0;
}

/**
 * @brief Creates evenly-sized divisions for an external (non-mzML) file.
 * @param input_filesize Size of the input file in bytes.
 * @param n_divisions Number of divisions to create.
 * @return Pointer to a newly allocated divisions_t.
 *
 * Each division covers a contiguous byte range of the file, stored as a single
 * XML data_positions_t entry. The last division extends to the end of the file.
 *
 * @warning The caller must free with dealloc_divisions().
 */
divisions_t* divide_external(long input_filesize, int n_divisions) {
   divisions_t* r = malloc(sizeof(divisions_t));

   r->divisions = malloc(sizeof(division_t) * n_divisions);
   r->n_divisions = n_divisions;

   uint64_t division_size = input_filesize / n_divisions;

   for (int i = 0; i < n_divisions; i++) {
      division_t* curr;
      r->divisions[i] =
          alloc_division(1, 0, 0);  // One "XML" section for each division
      curr = r->divisions[i];

      curr->xml->start_positions[0] = i * division_size;
      curr->xml->end_positions[0] = (i + 1) * division_size;
      curr->xml->total_spec = 1;

      if (i == n_divisions - 1)
         curr->xml->end_positions[0] = input_filesize;

      curr->size = curr->xml->end_positions[0] - curr->xml->start_positions[0];
   }

   return r;
}

/**
 * @brief Creates a data_format_t configured for external (non-mzML) file compression.
 * @return Pointer to a newly allocated data_format_t with ZSTD defaults.
 *
 * Sets the XML format to ZSTD. The m/z and intensity formats are set to ZSTD
 * as placeholders required for a valid MSZ file, but are unused for external files.
 *
 * @warning The caller must free with dealloc_df().
 */
data_format_t* create_external_df() {
   data_format_t* df = calloc(1, sizeof(data_format_t));

   df->target_xml_format = _ZSTD_compression_;

   /* The following are not actually used, required for valid msz file. */
   df->source_compression = _no_comp_;
   df->target_mz_format = _ZSTD_compression_;
   df->target_inten_format = _ZSTD_compression_;

   return df;
}

/**
 * @brief Preprocesses an external (non-mzML) file for compression.
 * @param input_map Pointer to the memory-mapped input file (unused, kept for API consistency).
 * @param input_filesize Size of the input file in bytes.
 * @param blocksize Pointer to the target block size (unused for external files).
 * @param arguments Pointer to the Arguments struct (thread count used for division count).
 * @param df Output pointer set to the created data_format_t.
 * @param divisions Output pointer set to the created divisions_t.
 * @return 0 on success.
 */
int preprocess_external(char* input_map, long input_filesize, long* blocksize,
                        Arguments* arguments, data_format_t** df,
                        divisions_t** divisions) {
   double start, end;

   start = get_time();

   print("\nPreprocessing...\n");

   *df = create_external_df();

   divisions_t* div = divide_external(input_filesize, arguments->threads);

   set_compress_runtime_variables(arguments, *df);

   *divisions = div;

   return 0;
}

/**
 * @brief Parses the MSZ file footer and reconstructs block length queues and divisions.
 * @param footer Output pointer set to the parsed footer_t.
 * @param input_map Pointer to the memory-mapped MSZ file.
 * @param input_filesize Size of the input file in bytes.
 * @param xml_block_lens Output pointer set to the XML block length queue.
 * @param mz_binary_block_lens Output pointer set to the m/z block length queue.
 * @param inten_binary_block_lens Output pointer set to the intensity block length queue.
 * @param divisions Output pointer set to the deserialized divisions_t.
 * @param n_divisions Output pointer set to the number of divisions.
 *
 * @note The block length queues and divisions reference mmap'd data internally.
 */
void parse_footer(footer_t** footer, void* input_map, long input_filesize,
                  block_len_queue_t** xml_block_lens,
                  block_len_queue_t** mz_binary_block_lens,
                  block_len_queue_t** inten_binary_block_lens,
                  divisions_t** divisions, int* n_divisions) {
   *footer = read_footer(input_map, input_filesize);

   print("\tXML position: %ld\n", (*footer)->xml_pos);
   print("\tm/z binary position: %ld\n", (*footer)->mz_binary_pos);
   print("\tint binary position: %ld\n", (*footer)->inten_binary_pos);
   print("\tXML blocks position: %ld\n", (*footer)->xml_blk_pos);
   print("\tm/z binary blocks position: %ld\n", (*footer)->mz_binary_blk_pos);
   print("\tinten binary blocks position: %ld\n",
         (*footer)->inten_binary_blk_pos);
   print("\tdivisions position: %ld\n", (*footer)->divisions_t_pos);
   print("\tEOF position: %ld\n", input_filesize);
   print("\tOriginal filesize: %ld\n", (*footer)->original_filesize);

   *xml_block_lens = read_block_len_queue(input_map, (*footer)->xml_blk_pos,
                                          (*footer)->mz_binary_blk_pos);
   *mz_binary_block_lens =
       read_block_len_queue(input_map, (*footer)->mz_binary_blk_pos,
                            (*footer)->inten_binary_blk_pos);
   *inten_binary_block_lens = read_block_len_queue(
       input_map, (*footer)->inten_binary_blk_pos, (*footer)->divisions_t_pos);

   *n_divisions = (*footer)->n_divisions;

   *divisions =
       read_divisions(input_map, (*footer)->divisions_t_pos, *n_divisions);
}