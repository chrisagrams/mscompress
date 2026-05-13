/**
 * @file mszx.c
 * @brief MSZX (tar archive) decompression: extract embedded MSZ to mzML
 *        and write annotation entries to an output directory.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mscompress.h"

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#include <io.h>
#define close _close
#define read _read
#define write _write
#define lseek64 _lseeki64
#define PATH_SEP '\\'
#else
#include <sys/mman.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

/* Cross-platform directory create. Returns 0 on success or if it already
 * exists; -1 on error. */
static int ensure_dir(const char* path) {
   if (!path || !*path) return -1;
#ifdef _WIN32
   if (_mkdir(path) == 0) return 0;
#else
   if (mkdir(path, 0755) == 0) return 0;
#endif
   if (errno == EEXIST) return 0;
   return -1;
}

/* Return basename of path (pointer into path). Handles both / and \. */
static const char* path_basename(const char* path) {
   const char* base = path;
   for (const char* p = path; *p; ++p) {
      if (*p == '/' || *p == '\\') base = p + 1;
   }
   return base;
}

/* Build "<dir>/<name>" into a fresh malloc'd buffer. */
static char* join_path(const char* dir, const char* name) {
   size_t dlen = strlen(dir);
   size_t nlen = strlen(name);
   int need_sep = dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\';
   char* out = malloc(dlen + (need_sep ? 1 : 0) + nlen + 1);
   if (!out) return NULL;
   memcpy(out, dir, dlen);
   size_t off = dlen;
   if (need_sep) out[off++] = PATH_SEP;
   memcpy(out + off, name, nlen);
   out[off + nlen] = '\0';
   return out;
}

/* Returns 1 if str ends with suffix (case-sensitive), else 0. */
static int ends_with(const char* str, const char* suffix) {
   size_t slen = strlen(str);
   size_t xlen = strlen(suffix);
   if (xlen > slen) return 0;
   return strcmp(str + slen - xlen, suffix) == 0;
}

typedef struct {
   mszx_entry_t* items;
   size_t count;
   size_t cap;
} entry_list_t;

static int collect_cb(const char* name, int64_t data_offset, size_t data_size,
                      void* user) {
   entry_list_t* list = (entry_list_t*)user;
   if (list->count == list->cap) {
      size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
      mszx_entry_t* tmp = realloc(list->items, new_cap * sizeof(*tmp));
      if (!tmp) return 1; /* stop walk on OOM */
      list->items = tmp;
      list->cap = new_cap;
   }
   list->items[list->count].name = strdup(name);
   if (!list->items[list->count].name) return 1;
   list->items[list->count].offset = data_offset;
   list->items[list->count].size = data_size;
   list->count++;
   return 0;
}

void mszx_free_entries(mszx_entry_t* entries, size_t count) {
   if (!entries) return;
   for (size_t i = 0; i < count; ++i) free(entries[i].name);
   free(entries);
}

int mszx_list_entries(int fd, mszx_entry_t** out, size_t* out_count) {
   if (fd < 0 || !out || !out_count) return -1;
   entry_list_t list = {NULL, 0, 0};
   int rc = walk_tar_entries(fd, collect_cb, &list);
   if (rc != 0) {
      mszx_free_entries(list.items, list.count);
      return -1;
   }
   *out = list.items;
   *out_count = list.count;
   return 0;
}

/* Stream-copy `length` bytes from fd@offset to a newly-created output file.
 * Used for annotation entries that are NOT zstd-compressed. */
static int copy_entry_raw(int in_fd, int64_t offset, size_t length,
                          const char* out_path) {
   int out_fd = open_output_file((char*)out_path);
   if (out_fd < 0) return -1;

#ifdef _WIN32
   if (_lseeki64(in_fd, offset, SEEK_SET) < 0) { close_file(out_fd); return -1; }
#else
   if (lseek(in_fd, (off_t)offset, SEEK_SET) < 0) { close_file(out_fd); return -1; }
#endif

   char buf[65536];
   size_t remaining = length;
   while (remaining > 0) {
      size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
      ssize_t n = read(in_fd, buf, (unsigned int)want);
      if (n <= 0) { close_file(out_fd); return -1; }
      if (write_to_file(out_fd, buf, (size_t)n) != (size_t)n) {
         close_file(out_fd);
         return -1;
      }
      remaining -= (size_t)n;
   }
   close_file(out_fd);
   return 0;
}

/* Read the entire entry (compressed) into memory, ZSTD-decompress in one
 * shot, write to out_path. Annotations are small (KB to low-MB), so
 * one-shot decompress is fine. */
static int copy_entry_zstd(int in_fd, int64_t offset, size_t length,
                           const char* out_path) {
   if (length == 0) return -1;

   char* compressed = malloc(length);
   if (!compressed) return -1;

#ifdef _WIN32
   if (_lseeki64(in_fd, offset, SEEK_SET) < 0) { free(compressed); return -1; }
#else
   if (lseek(in_fd, (off_t)offset, SEEK_SET) < 0) { free(compressed); return -1; }
#endif

   size_t got = 0;
   while (got < length) {
      ssize_t n = read(in_fd, compressed + got, (unsigned int)(length - got));
      if (n <= 0) { free(compressed); return -1; }
      got += (size_t)n;
   }

   uint64_t decompressed_size = ZSTD_getFrameContentSize(compressed, length);
   if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
       decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
      free(compressed);
      return -1;
   }

   char* decompressed = malloc((size_t)decompressed_size);
   if (!decompressed) { free(compressed); return -1; }

   size_t result = ZSTD_decompress(decompressed, (size_t)decompressed_size,
                                   compressed, length);
   free(compressed);
   if (ZSTD_isError(result)) { free(decompressed); return -1; }

   int out_fd = open_output_file((char*)out_path);
   if (out_fd < 0) { free(decompressed); return -1; }

   size_t wrote = write_to_file(out_fd, decompressed, result);
   close_file(out_fd);
   free(decompressed);
   return wrote == result ? 0 : -1;
}

/* Build "<output_dir>/<basename(input_path) minus .mszx>.mzML". */
static char* derive_mzml_path(const char* input_path, const char* output_dir) {
   const char* base = path_basename(input_path);
   size_t blen = strlen(base);
   /* Strip trailing ".mszx" if present. */
   const char* suffix = ".mszx";
   size_t slen = strlen(suffix);
   if (blen > slen && strcmp(base + blen - slen, suffix) == 0) {
      blen -= slen;
   }
   /* Compose <base_no_ext> + ".mzML". */
   const char* mzml_ext = ".mzML";
   size_t elen = strlen(mzml_ext);
   char* fname = malloc(blen + elen + 1);
   if (!fname) return NULL;
   memcpy(fname, base, blen);
   memcpy(fname + blen, mzml_ext, elen);
   fname[blen + elen] = '\0';

   char* full = join_path(output_dir, fname);
   free(fname);
   return full;
}

/**
 * @brief Decompress a .mszx archive: write the embedded MSZ as mzML and
 *        extract annotation files into an output directory.
 *
 * The first entry whose name ends in ".msz" is treated as the spectra
 * payload and decompressed via the standard MSZ decompression pipeline.
 * Every other regular-file entry (except "manifest.json") is written to
 * <output_dir>/<entry_name>; if the name ends with ".zst" the suffix is
 * stripped and the file is ZSTD-decompressed during the copy.
 *
 * @param input_path Path to the .mszx archive.
 * @param output_dir Directory to write outputs into. Created if missing.
 * @param arguments Runtime arguments forwarded to decompress_msz.
 *
 * @return 0 on success, non-zero on failure.
 */
int decompress_mszx(char* input_path, char* output_dir, Arguments* arguments) {
   if (!input_path || !output_dir || !arguments) return -1;

   if (ensure_dir(output_dir) != 0) {
      warning("decompress_mszx: could not create output directory '%s'\n",
              output_dir);
      return -1;
   }

   int fd = open_input_file(input_path);
   if (fd < 0) {
      warning("decompress_mszx: could not open '%s'\n", input_path);
      return -1;
   }

   mszx_entry_t* entries = NULL;
   size_t n_entries = 0;
   if (mszx_list_entries(fd, &entries, &n_entries) != 0) {
      warning("decompress_mszx: '%s' is not a valid USTAR archive\n",
              input_path);
      close_file(fd);
      return -1;
   }

   /* Locate the spectra entry (first .msz). */
   ssize_t spectra_idx = -1;
   for (size_t i = 0; i < n_entries; ++i) {
      if (ends_with(entries[i].name, ".msz")) {
         spectra_idx = (ssize_t)i;
         break;
      }
   }
   if (spectra_idx < 0) {
      warning("decompress_mszx: no .msz entry found in '%s'\n", input_path);
      mszx_free_entries(entries, n_entries);
      close_file(fd);
      return -1;
   }

   /* Decompress the MSZ via offset-mmap into the tar. */
   mszx_entry_t* msz = &entries[spectra_idx];
   size_t pad = 0, map_length = 0;
   void* base = get_mapping_range(fd, msz->offset, msz->size, &pad, &map_length);
   if (!base) {
      warning("decompress_mszx: failed to mmap MSZ entry from '%s'\n",
              input_path);
      mszx_free_entries(entries, n_entries);
      close_file(fd);
      return -1;
   }

   char* mzml_path = derive_mzml_path(input_path, output_dir);
   if (!mzml_path) {
      remove_mapping(base, map_length);
      mszx_free_entries(entries, n_entries);
      close_file(fd);
      return -1;
   }

   int out_fd = open_output_file(mzml_path);
   if (out_fd < 0) {
      warning("decompress_mszx: could not open output mzML '%s'\n", mzml_path);
      free(mzml_path);
      remove_mapping(base, map_length);
      mszx_free_entries(entries, n_entries);
      close_file(fd);
      return -1;
   }

   print("\tDecompressing MSZ entry to %s\n", mzml_path);
   int rc = decompress_msz((char*)base + pad, msz->size, arguments, out_fd);
   close_file(out_fd);
   free(mzml_path);
   remove_mapping(base, map_length);

   if (rc != 0) {
      warning("decompress_mszx: decompress_msz failed\n");
      mszx_free_entries(entries, n_entries);
      close_file(fd);
      return -1;
   }

   /* Extract annotation entries. Skip the spectra entry and manifest.json. */
   for (size_t i = 0; i < n_entries; ++i) {
      if ((ssize_t)i == spectra_idx) continue;
      if (strcmp(entries[i].name, "manifest.json") == 0) continue;

      const char* entry_name = entries[i].name;
      int is_zst = ends_with(entry_name, ".zst");

      /* Strip .zst from output name if present. */
      char* out_name = NULL;
      if (is_zst) {
         size_t nlen = strlen(entry_name);
         out_name = malloc(nlen - 3); /* strip ".zst" but keep NUL */
         if (!out_name) { rc = -1; break; }
         memcpy(out_name, entry_name, nlen - 4);
         out_name[nlen - 4] = '\0';
      } else {
         out_name = strdup(entry_name);
         if (!out_name) { rc = -1; break; }
      }

      char* out_path = join_path(output_dir, out_name);
      free(out_name);
      if (!out_path) { rc = -1; break; }

      print("\tExtracting %s -> %s\n", entry_name, out_path);

      int copy_rc = is_zst
                        ? copy_entry_zstd(fd, entries[i].offset,
                                          entries[i].size, out_path)
                        : copy_entry_raw(fd, entries[i].offset,
                                         entries[i].size, out_path);
      if (copy_rc != 0) {
         warning("decompress_mszx: failed to extract '%s'\n", entry_name);
         free(out_path);
         rc = -1;
         break;
      }
      free(out_path);
   }

   mszx_free_entries(entries, n_entries);
   close_file(fd);
   return rc;
}
