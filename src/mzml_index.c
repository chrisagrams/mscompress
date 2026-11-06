/*
 * indexedmzML index handling.
 *
 * An indexedmzML file ends with a wrapper tail after </mzML>: <indexList> with
 * the byte offset of every <spectrum> and <chromatogram>, <indexListOffset>,
 * <fileChecksum> and </indexedmzML>. Decompression re-encodes binaries and
 * extraction drops spectra, so the stored tail would point at the wrong bytes.
 * Compression still stores the source tail (so readers that predate this keep
 * writing it, stale index and all), and adds a small section before the
 * footer with what the index cannot be rebuilt without: chromatogram
 * positions, the source checksum, and the stored tail's length. A flag in the
 * header's reserved bytes (MZML_INDEX_FLAG_OFFSET) marks the section.
 * Decompression drops the stored tail and writes a fresh one from where each
 * element actually landed in the output.
 *
 * Section layout (native byte order, like the rest of the .msz format):
 *
 *    char     magic[8]           "MSZMZIDX"
 *    uint32_t flags              MSZ_INDEX_REBUILD
 *    uint32_t checksum_len
 *    uint64_t tail_len           stored bytes after </mzML>
 *    uint64_t n_chrom
 *    uint64_t chrom_offsets[n_chrom]
 *    char     checksum[checksum_len]
 *    uint64_t section_pos        offset of `magic`; last 8 bytes before footer
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"

#define INDEX_MAGIC "MSZMZIDX"
#define INDEX_MAGIC_LEN 8

static int is_ws(char c) {
   return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* First occurrence of needle[0..n) in [p, end), or NULL. */
static const char* find_bytes(const char* p, const char* end,
                              const char* needle, size_t n) {
   while (p < end && (size_t)(end - p) >= n) {
      const char* q = memchr(p, needle[0], (size_t)(end - p) - n + 1);
      if (q == NULL)
         return NULL;
      if (memcmp(q, needle, n) == 0)
         return q;
      p = q + 1;
   }
   return NULL;
}

/* True if p (with avail bytes) starts with "<name" followed by whitespace. */
static int is_open_tag(const char* p, size_t avail, const char* name,
                       size_t n) {
   return avail > n && memcmp(p, name, n) == 0 && is_ws(p[n]);
}

/**
 * @brief Inspects the part of an mzML file after its last spectrum.
 *
 * Records the offset of every <chromatogram> before </mzML>, and checks
 * whether an indexedmzML <indexList> follows </mzML>. Only then is the index
 * rebuilt: plain mzML, or an indexedmzML file without an index (truncated or
 * written without one), keeps its tail verbatim.
 *
 * @param input_map The mzML file.
 * @param from Where to start looking (end of the last spectrum, or 0).
 * @param end End of the file.
 * @param cut Set to the offset just past "</mzML>" when an index follows
 *            (info->tail_len is then end - cut).
 * @return Tail information when the index should be rebuilt, otherwise NULL.
 *         Free with dealloc_mzml_index_info().
 */
mzml_index_info_t* scan_mzml_tail(const char* input_map, uint64_t from,
                                  uint64_t end, uint64_t* cut) {
   if (input_map == NULL || from > end)
      return NULL;

   const char* p = input_map + from;
   const char* e = input_map + end;

   const char* close = find_bytes(p, e, "</mzML>", 7);
   if (close == NULL)
      return NULL;
   const char* after = close + 7;

   const char* q = after;
   while (q < e && is_ws(*q))
      q++;
   if ((size_t)(e - q) < 10 || memcmp(q, "<indexList", 10) != 0)
      return NULL;

   mzml_index_info_t* info = calloc(1, sizeof(mzml_index_info_t));
   if (info == NULL)
      return NULL;
   info->flags = MSZ_INDEX_REBUILD;

   size_t cap = 0;
   const char* c = p;
   while ((c = find_bytes(c, close, "<chromatogram", 13)) != NULL) {
      if (is_open_tag(c, (size_t)(close - c), "<chromatogram", 13)) {
         if (info->n_chrom == cap) {
            cap = cap ? cap * 2 : 16;
            uint64_t* tmp =
                realloc(info->chrom_offsets, cap * sizeof(uint64_t));
            if (tmp == NULL) {
               dealloc_mzml_index_info(info);
               return NULL;
            }
            info->chrom_offsets = tmp;
         }
         info->chrom_offsets[info->n_chrom++] = (uint64_t)(c - input_map);
      }
      c += 13;
   }

   const char* cs = find_bytes(after, e, "<fileChecksum>", 14);
   if (cs != NULL) {
      cs += 14;
      const char* ce = find_bytes(cs, e, "</fileChecksum>", 15);
      if (ce != NULL) {
         info->checksum_len = (size_t)(ce - cs);
         info->checksum = malloc(info->checksum_len + 1);
         if (info->checksum == NULL) {
            dealloc_mzml_index_info(info);
            return NULL;
         }
         memcpy(info->checksum, cs, info->checksum_len);
         info->checksum[info->checksum_len] = '\0';
      }
   }

   *cut = (uint64_t)(after - input_map);
   info->tail_len = end - *cut;
   return info;
}

void dealloc_mzml_index_info(mzml_index_info_t* info) {
   if (info == NULL)
      return;
   free(info->chrom_offsets);
   free(info->checksum);
   free(info);
}

/** @return Non-zero when the index of the file described by df is rebuilt. */
int mzml_index_rebuild(data_format_t* df) {
   return df != NULL && df->index_info != NULL &&
          (df->index_info->flags & MSZ_INDEX_REBUILD);
}

/**
 * @brief Writes the index section (see the layout at the top of this file).
 * @param section_pos Offset in the .msz at which the section starts.
 * @return Bytes written.
 */
size_t mzml_index_write_section(mzml_index_info_t* info, uint64_t section_pos,
                                int fd) {
   uint32_t checksum_len = (uint32_t)info->checksum_len;
   size_t len = INDEX_MAGIC_LEN + 2 * sizeof(uint32_t) + 2 * sizeof(uint64_t) +
                info->n_chrom * sizeof(uint64_t) + checksum_len +
                sizeof(uint64_t);
   char* buff = malloc(len);
   if (buff == NULL) {
      MSZ_ERROR("mzml_index_write_section: malloc failed.\n");
      return 0;
   }

   size_t off = 0;
   memcpy(buff + off, INDEX_MAGIC, INDEX_MAGIC_LEN);
   off += INDEX_MAGIC_LEN;
   memcpy(buff + off, &info->flags, sizeof(uint32_t));
   off += sizeof(uint32_t);
   memcpy(buff + off, &checksum_len, sizeof(uint32_t));
   off += sizeof(uint32_t);
   memcpy(buff + off, &info->tail_len, sizeof(uint64_t));
   off += sizeof(uint64_t);
   memcpy(buff + off, &info->n_chrom, sizeof(uint64_t));
   off += sizeof(uint64_t);
   if (info->n_chrom > 0) {
      memcpy(buff + off, info->chrom_offsets, info->n_chrom * sizeof(uint64_t));
      off += info->n_chrom * sizeof(uint64_t);
   }
   if (checksum_len > 0) {
      memcpy(buff + off, info->checksum, checksum_len);
      off += checksum_len;
   }
   memcpy(buff + off, &section_pos, sizeof(uint64_t));
   off += sizeof(uint64_t);

   write_to_file(fd, buff, off);
   free(buff);
   return off;
}

/**
 * @brief Reads the index section of an .msz whose header flags one.
 * @return The section's contents, or NULL (with an error logged) if it is
 *         missing or malformed. Free with dealloc_mzml_index_info().
 */
mzml_index_info_t* mzml_index_read_section(void* input_map, long filesize) {
   const char* map = (const char*)input_map;
   size_t fixed = INDEX_MAGIC_LEN + 2 * sizeof(uint32_t) + 2 * sizeof(uint64_t);

   if (filesize < (long)(sizeof(footer_t) + sizeof(uint64_t) + fixed)) {
      MSZ_ERROR("mzml_index_read_section: file too small.\n");
      return NULL;
   }

   uint64_t limit = (uint64_t)filesize - sizeof(footer_t) - sizeof(uint64_t);
   uint64_t pos;
   memcpy(&pos, map + limit, sizeof(uint64_t));

   if (pos > limit || limit - pos < fixed ||
       memcmp(map + pos, INDEX_MAGIC, INDEX_MAGIC_LEN) != 0) {
      MSZ_ERROR("mzml_index_read_section: index section not found.\n");
      return NULL;
   }

   mzml_index_info_t* info = calloc(1, sizeof(mzml_index_info_t));
   if (info == NULL)
      return NULL;

   uint32_t checksum_len;
   uint64_t off = pos + INDEX_MAGIC_LEN;
   memcpy(&info->flags, map + off, sizeof(uint32_t));
   off += sizeof(uint32_t);
   memcpy(&checksum_len, map + off, sizeof(uint32_t));
   off += sizeof(uint32_t);
   memcpy(&info->tail_len, map + off, sizeof(uint64_t));
   off += sizeof(uint64_t);
   memcpy(&info->n_chrom, map + off, sizeof(uint64_t));
   off += sizeof(uint64_t);

   uint64_t remaining = limit - off;
   if (info->n_chrom > remaining / sizeof(uint64_t) ||
       info->n_chrom * sizeof(uint64_t) + checksum_len != remaining) {
      MSZ_ERROR("mzml_index_read_section: index section is corrupt.\n");
      free(info);
      return NULL;
   }

   if (info->n_chrom > 0) {
      info->chrom_offsets = malloc(info->n_chrom * sizeof(uint64_t));
      if (info->chrom_offsets == NULL) {
         free(info);
         return NULL;
      }
      memcpy(info->chrom_offsets, map + off, info->n_chrom * sizeof(uint64_t));
      off += info->n_chrom * sizeof(uint64_t);
   }

   if (checksum_len > 0) {
      info->checksum = malloc(checksum_len + 1);
      if (info->checksum == NULL) {
         dealloc_mzml_index_info(info);
         return NULL;
      }
      memcpy(info->checksum, map + off, checksum_len);
      info->checksum[checksum_len] = '\0';
      info->checksum_len = checksum_len;
   }

   return info;
}

mzml_index_builder_t* alloc_mzml_index_builder(void) {
   return calloc(1, sizeof(mzml_index_builder_t));
}

void dealloc_mzml_index_builder(mzml_index_builder_t* b) {
   if (b == NULL)
      return;
   free(b->spec);
   free(b->chrom);
   free(b);
}

static int append(char** buf, size_t* len, size_t* cap, const char* src,
                  size_t n) {
   if (*len + n > *cap) {
      size_t ncap = *cap ? *cap : 1 << 16;
      while (ncap < *len + n)
         ncap *= 2;
      char* tmp = realloc(*buf, ncap);
      if (tmp == NULL)
         return 1;
      *buf = tmp;
      *cap = ncap;
   }
   memcpy(*buf + *len, src, n);
   *len += n;
   return 0;
}

/**
 * @brief Adds an <offset> entry for the element whose opening tag is at `tag`.
 *
 * The idRef is the element's id attribute, copied as written (it is already
 * XML-escaped).
 *
 * @param kind MZML_INDEX_SPECTRUM or MZML_INDEX_CHROMATOGRAM.
 * @param tag Start of the opening tag ("<spectrum ..." / "<chromatogram ...").
 * @param avail Bytes readable from `tag`.
 * @param offset Output offset of the opening tag.
 * @return 0 on success, 1 if no id attribute was found or on allocation failure.
 */
int mzml_index_add(mzml_index_builder_t* b, int kind, const char* tag,
                   size_t avail, uint64_t offset) {
   const char* gt = memchr(tag, '>', avail);
   if (gt == NULL)
      return 1;

   const char* p = tag;
   const char* id = NULL;
   while ((p = find_bytes(p, gt, "id=", 3)) != NULL) {
      if (p > tag && is_ws(p[-1])) {
         id = p + 3;
         break;
      }
      p += 3;
   }
   if (id == NULL || id >= gt || (*id != '"' && *id != '\''))
      return 1;

   char quote = *id++;
   const char* id_end = memchr(id, quote, (size_t)(gt - id));
   if (id_end == NULL)
      return 1;

   char** buf = kind == MZML_INDEX_SPECTRUM ? &b->spec : &b->chrom;
   size_t* len = kind == MZML_INDEX_SPECTRUM ? &b->spec_len : &b->chrom_len;
   size_t* cap = kind == MZML_INDEX_SPECTRUM ? &b->spec_cap : &b->chrom_cap;

   char num[48];
   int num_len = snprintf(num, sizeof(num), "\">%" PRIu64 "</offset>\n", offset);

   if (append(buf, len, cap, "      <offset idRef=\"", 21))
      return 1;
   /* A single-quoted id may contain '"', which a double-quoted idRef cannot. */
   for (const char* s = id; s < id_end;) {
      const char* dq = memchr(s, '"', (size_t)(id_end - s));
      const char* stop = dq ? dq : id_end;
      if (append(buf, len, cap, s, (size_t)(stop - s)))
         return 1;
      if (dq == NULL)
         break;
      if (append(buf, len, cap, "&quot;", 6))
         return 1;
      s = dq + 1;
   }
   if (append(buf, len, cap, num, (size_t)num_len))
      return 1;

   if (kind == MZML_INDEX_SPECTRUM)
      b->n_spec++;
   else
      b->n_chrom++;
   return 0;
}

/* Last "<name" opening tag in [lo, hi), or NULL. */
static const char* find_last_tag(const char* lo, const char* hi,
                                 const char* name, size_t n) {
   for (const char* p = hi - n - 1; p >= lo; p--)
      if (*p == '<' && is_open_tag(p, (size_t)(hi - p), name, n))
         return p;
   return NULL;
}

/**
 * @brief Adds the spectra and chromatograms of one reconstructed division.
 *
 * Spectrum k of a division starts inside XML segment 2k (the XML between the
 * previous binary and its m/z binary), at the same distance from the segment
 * start as in the source, so its output offset follows from where that
 * segment was written. Chromatograms are located the same way from their
 * recorded source offsets.
 *
 * @param info Tail information (chromatogram offsets); may be NULL.
 * @param div The division as stored in the .msz (or produced by scan_mzml).
 * @param xml_out Output offset, within buff, of each of div's XML segments.
 * @param buff The division's reconstructed output.
 * @param buff_len Length of buff.
 * @param base Output offset of buff[0] in the whole file.
 * @return Number of elements that could not be indexed.
 */
int mzml_index_add_division(mzml_index_builder_t* b,
                            const mzml_index_info_t* info,
                            const division_t* div, const int64_t* xml_out,
                            const char* buff, size_t buff_len, uint64_t base) {
   int missed = 0;
   const data_positions_t* spectra = div->spectra;
   const data_positions_t* xml = div->xml;

   for (int k = 0; spectra != NULL && k < spectra->total_spec; k++) {
      uint64_t s = spectra->start_positions[k];
      uint64_t e = spectra->end_positions[k];
      if (e <= s)  // placeholder entry (see extract_n_spectra)
         continue;

      int64_t j = 2 * (int64_t)k;
      if (j >= xml->total_spec)
         break;
      uint64_t xs = xml->start_positions[j], xe = xml->end_positions[j];
      size_t seg_lo = (size_t)xml_out[j], seg_hi = seg_lo + (size_t)(xe - xs);
      if (seg_hi > buff_len) {
         missed++;
         continue;
      }

      const char* tag = NULL;
      if (s >= xs && s < xe) {
         const char* t = buff + seg_lo + (s - xs);
         if (is_open_tag(t, buff_len - (size_t)(t - buff), "<spectrum", 9))
            tag = t;
      }
      if (tag == NULL)  // fall back to the last <spectrum> in the segment
         tag = find_last_tag(buff + seg_lo, buff + seg_hi, "<spectrum", 9);

      if (tag == NULL ||
          mzml_index_add(b, MZML_INDEX_SPECTRUM, tag,
                         buff_len - (size_t)(tag - buff),
                         base + (uint64_t)(tag - buff)) != 0)
         missed++;
   }

   if (info == NULL || b->next_chrom >= info->n_chrom)
      return missed;

   for (int64_t j = 0; j < xml->total_spec && b->next_chrom < info->n_chrom;
        j++) {
      uint64_t xs = xml->start_positions[j], xe = xml->end_positions[j];
      while (b->next_chrom < info->n_chrom) {
         uint64_t c = info->chrom_offsets[b->next_chrom];
         if (c < xs || c >= xe)
            break;
         size_t local = (size_t)xml_out[j] + (size_t)(c - xs);
         if (local >= buff_len ||
             mzml_index_add(b, MZML_INDEX_CHROMATOGRAM, buff + local,
                            buff_len - local, base + local) != 0)
            missed++;
         b->next_chrom++;
      }
   }
   return missed;
}

/**
 * @brief Adds the chromatograms inside a contiguous run of source XML.
 *
 * For writers that copy a source range [src_start, src_end) unchanged to
 * output offset `base` (buff holds that range).
 *
 * @return Number of chromatograms that could not be indexed.
 */
int mzml_index_add_chromatograms(mzml_index_builder_t* b,
                                 const mzml_index_info_t* info,
                                 uint64_t src_start, uint64_t src_end,
                                 const char* buff, size_t buff_len,
                                 uint64_t base) {
   int missed = 0;
   if (info == NULL)
      return 0;
   for (uint64_t i = 0; i < info->n_chrom; i++) {
      uint64_t c = info->chrom_offsets[i];
      if (c < src_start || c >= src_end)
         continue;
      size_t local = (size_t)(c - src_start);
      if (local >= buff_len ||
          mzml_index_add(b, MZML_INDEX_CHROMATOGRAM, buff + local,
                         buff_len - local, base + local) != 0)
         missed++;
   }
   return missed;
}

/**
 * @brief Writes the indexedmzML wrapper tail that follows </mzML>.
 *
 * Uses the layout ProteoWizard writes, so an unchanged file round-trips
 * byte-for-byte.
 *
 * @param written Output offset just past "</mzML>".
 * @return Bytes written.
 */
size_t mzml_index_write_tail(mzml_index_builder_t* b,
                             const mzml_index_info_t* info, uint64_t written,
                             int fd) {
   char tmp[256];
   size_t total = 0;
   int n;
   int count = (b->n_spec > 0) + (b->n_chrom > 0);

   /* "\n  " precedes <indexList>, which is what indexListOffset points at. */
   uint64_t index_list_offset = written + 3;

   n = snprintf(tmp, sizeof(tmp), "\n  <indexList count=\"%d\">\n", count);
   write_to_file(fd, tmp, (size_t)n);
   total += (size_t)n;

   if (b->n_spec > 0) {
      write_to_file(fd, "    <index name=\"spectrum\">\n", 28);
      total += 28;
      write_to_file(fd, b->spec, b->spec_len);
      total += b->spec_len;
      write_to_file(fd, "    </index>\n", 13);
      total += 13;
   }
   if (b->n_chrom > 0) {
      write_to_file(fd, "    <index name=\"chromatogram\">\n", 32);
      total += 32;
      write_to_file(fd, b->chrom, b->chrom_len);
      total += b->chrom_len;
      write_to_file(fd, "    </index>\n", 13);
      total += 13;
   }

   n = snprintf(tmp, sizeof(tmp),
                "  </indexList>\n  <indexListOffset>%" PRIu64
                "</indexListOffset>\n",
                index_list_offset);
   write_to_file(fd, tmp, (size_t)n);
   total += (size_t)n;

   if (info != NULL && info->checksum != NULL) {
      write_to_file(fd, "  <fileChecksum>", 16);
      total += 16;
      write_to_file(fd, info->checksum, info->checksum_len);
      total += info->checksum_len;
      write_to_file(fd, "</fileChecksum>\n", 16);
      total += 16;
   }

   write_to_file(fd, "</indexedmzML>\n", 15);
   total += 15;
   return total;
}

/**
 * @brief Loads the index section of an .msz into df->index_info.
 *
 * Only files whose header flags it have one; otherwise df->index_info is left
 * NULL and the stored tail is written as before.
 *
 * @return 0 on success (including "no section"), non-zero if a flagged
 *         file's section is missing or corrupt.
 */
int mzml_index_load(void* input_map, long filesize, data_format_t* df) {
   uint32_t flag = 0;
   if (filesize < HEADER_SIZE)
      return 0;
   memcpy(&flag, (const char*)input_map + MZML_INDEX_FLAG_OFFSET, sizeof(flag));
   if (!(flag & MSZ_INDEX_REBUILD))
      return 0;
   dealloc_mzml_index_info(df->index_info);
   df->index_info = mzml_index_read_section(input_map, filesize);
   return df->index_info == NULL ? 1 : 0;
}

/**
 * @brief Drops the stored source tail from the end of the final output block.
 *
 * @param buff Output block that ends the file.
 * @param len In: its length. Out: the length up to and including </mzML>.
 * @return 0 on success; 1 (len unchanged) if buff does not end with a tail
 *         of info->tail_len bytes preceded by </mzML>.
 */
int mzml_index_trim_tail(const mzml_index_info_t* info, const char* buff,
                         size_t* len) {
   if (*len < info->tail_len + 7)
      return 1;
   size_t keep = *len - (size_t)info->tail_len;
   if (memcmp(buff + keep - 7, "</mzML>", 7) != 0)
      return 1;
   *len = keep;
   return 0;
}
