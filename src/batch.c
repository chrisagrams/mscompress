/**
 * @file batch.c
 * @brief Batch compression: many mzML (folder / glob / explicit list /
 *        --from-file) -> ONE .mszx tar container of independent .msz entries.
 *
 * Writer strategy is Option A (streaming seek-back-and-patch), the ONLY writer
 * path: each entry's compressed .msz is streamed straight into the archive fd
 * via the unmodified compress_mzml(), then the USTAR header's size + checksum
 * are patched in place (tar_begin_entry / tar_end_entry in file.c). This
 * requires a seekable regular-file output; a non-seekable output is a hard
 * error (no temp-file fallback, no --jobs). See BATCH_MODE_PLAN.md.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mscompress.h"

#ifdef _WIN32
#include <io.h>
#define PATH_SEP '/'
#else
#include <dirent.h>
#include <glob.h>
#include <unistd.h>
#define PATH_SEP '/'
#endif

/* ---------------- small growable string list ---------------- */

typedef struct {
   char** items;
   size_t count;
   size_t cap;
} strlist_t;

static int strlist_push(strlist_t* l, const char* s) {
   if (l->count == l->cap) {
      size_t nc = l->cap ? l->cap * 2 : 16;
      char** tmp = realloc(l->items, nc * sizeof(*tmp));
      if (!tmp) return -1;
      l->items = tmp;
      l->cap = nc;
   }
   l->items[l->count] = strdup(s);
   if (!l->items[l->count]) return -1;
   l->count++;
   return 0;
}

static void strlist_free(strlist_t* l) {
   if (!l->items) return;
   for (size_t i = 0; i < l->count; ++i) free(l->items[i]);
   free(l->items);
   l->items = NULL;
   l->count = l->cap = 0;
}

/* ---------------- path helpers ---------------- */

static const char* base_name(const char* path) {
   const char* base = path;
   for (const char* p = path; *p; ++p)
      if (*p == '/' || *p == '\\') base = p + 1;
   return base;
}

static int str_iends_with(const char* s, const char* suffix) {
   size_t sl = strlen(s), xl = strlen(suffix);
   if (xl > sl) return 0;
   const char* a = s + sl - xl;
   for (size_t i = 0; i < xl; ++i) {
      char c = a[i], d = suffix[i];
      if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
      if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
      if (c != d) return 0;
   }
   return 1;
}

static int is_mzml_name(const char* path) {
   return str_iends_with(path, ".mzml");
}

static int is_directory(const char* path) {
   struct stat st;
   if (stat(path, &st) != 0) return 0;
   return (st.st_mode & S_IFMT) == S_IFDIR;
}

static int is_regular_file(const char* path) {
   struct stat st;
   if (stat(path, &st) != 0) return 0;
   return (st.st_mode & S_IFMT) == S_IFREG;
}

static int has_glob_meta(const char* s) {
   return strpbrk(s, "*?[") != NULL;
}

static char* join2(const char* dir, const char* name) {
   size_t dl = strlen(dir), nl = strlen(name);
   int sep = dl > 0 && dir[dl - 1] != '/' && dir[dl - 1] != '\\';
   char* out = malloc(dl + (sep ? 1 : 0) + nl + 1);
   if (!out) return NULL;
   memcpy(out, dir, dl);
   size_t o = dl;
   if (sep) out[o++] = PATH_SEP;
   memcpy(out + o, name, nl);
   out[o + nl] = '\0';
   return out;
}

/* ---------------- directory walk (POSIX) ---------------- */

#ifndef _WIN32
static int walk_dir(const char* dir, int recursive, strlist_t* out) {
   DIR* d = opendir(dir);
   if (!d) {
      warning("batch: cannot open directory '%s': %s\n", dir, strerror(errno));
      return -1;
   }
   struct dirent* ent;
   int rc = 0;
   while ((ent = readdir(d)) != NULL) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
         continue;
      char* full = join2(dir, ent->d_name);
      if (!full) { rc = -1; break; }
      if (is_directory(full)) {
         if (recursive) rc = walk_dir(full, recursive, out);
      } else if (is_regular_file(full) && is_mzml_name(full)) {
         if (strlist_push(out, full) != 0) rc = -1;
      }
      free(full);
      if (rc != 0) break;
   }
   closedir(d);
   return rc;
}
#endif

/* Resolve one input token (file | directory | glob) into `out`. */
static int resolve_token(const char* tok, int recursive, strlist_t* out) {
   if (has_glob_meta(tok)) {
#ifndef _WIN32
      glob_t g;
      int gr = glob(tok, 0, NULL, &g);
      if (gr == GLOB_NOMATCH) { globfree(&g); return 0; }
      if (gr != 0) {
         warning("batch: glob('%s') failed\n", tok);
         return -1;
      }
      int rc = 0;
      for (size_t i = 0; i < g.gl_pathc; ++i) {
         const char* m = g.gl_pathv[i];
         if (is_directory(m)) {
            rc = walk_dir(m, recursive, out);
         } else if (is_regular_file(m) && is_mzml_name(m)) {
            if (strlist_push(out, m) != 0) rc = -1;
         }
         if (rc != 0) break;
      }
      globfree(&g);
      return rc;
#else
      warning("batch: internal glob patterns are not supported on Windows "
              "yet ('%s'); pass explicit file paths\n",
              tok);
      return -1;
#endif
   }

   if (is_directory(tok)) {
#ifndef _WIN32
      return walk_dir(tok, recursive, out);
#else
      warning("batch: directory inputs are not supported on Windows yet "
              "('%s'); pass explicit file paths\n",
              tok);
      return -1;
#endif
   }

   if (is_regular_file(tok)) return strlist_push(out, tok);

   warning("batch: input '%s' not found (skipped)\n", tok);
   return 0;
}

/* Read newline-separated paths from a --from-file manifest ('-' = stdin). */
static int read_paths_from_file(const char* path, int recursive, strlist_t* out) {
   FILE* f = (strcmp(path, "-") == 0) ? stdin : fopen(path, "r");
   if (!f) {
      warning("batch: cannot open --from-file '%s': %s\n", path,
              strerror(errno));
      return -1;
   }
   char line[8192];
   int rc = 0;
   while (fgets(line, sizeof(line), f)) {
      size_t n = strlen(line);
      while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
         line[--n] = '\0';
      /* trim leading whitespace */
      char* p = line;
      while (*p == ' ' || *p == '\t') p++;
      if (*p == '\0' || *p == '#') continue; /* blank / comment */
      if (resolve_token(p, recursive, out) != 0) { rc = -1; break; }
   }
   if (f != stdin) fclose(f);
   return rc;
}

/* ---------------- sort + dedup ---------------- */

static int cmp_by_basename(const void* a, const void* b) {
   const char* pa = *(const char* const*)a;
   const char* pb = *(const char* const*)b;
   int c = strcmp(base_name(pa), base_name(pb));
   if (c != 0) return c;
   return strcmp(pa, pb);
}

static void sort_and_dedup(strlist_t* l) {
   if (l->count < 2) return;
   qsort(l->items, l->count, sizeof(l->items[0]), cmp_by_basename);
   size_t w = 1;
   for (size_t i = 1; i < l->count; ++i) {
      if (strcmp(l->items[i], l->items[w - 1]) == 0) {
         free(l->items[i]); /* exact duplicate path */
      } else {
         l->items[w++] = l->items[i];
      }
   }
   l->count = w;
}

/* ---------------- entry naming (flatten default) ---------------- */

/* Derive "<basename minus .mzML>.msz"; disambiguate collisions with __N. */
static char* derive_entry_name(const char* src, strlist_t* used) {
   const char* base = base_name(src);
   size_t bl = strlen(base);
   if (str_iends_with(base, ".mzml")) bl -= 5;
   char stem[512];
   size_t copy = bl < sizeof(stem) - 1 ? bl : sizeof(stem) - 1;
   memcpy(stem, base, copy);
   stem[copy] = '\0';

   char cand[600];
   snprintf(cand, sizeof(cand), "%s.msz", stem);
   int n = 2;
   int clash = 1;
   while (clash) {
      clash = 0;
      for (size_t i = 0; i < used->count; ++i) {
         if (strcmp(used->items[i], cand) == 0) { clash = 1; break; }
      }
      if (clash) snprintf(cand, sizeof(cand), "%s__%d.msz", stem, n++);
   }
   strlist_push(used, cand);
   return strdup(cand);
}

/* ---------------- manifest JSON (v2) ---------------- */

typedef struct {
   char* entry;
   char* original;
   uint64_t size;
} manifest_rec_t;

static void json_append_escaped(char** buf, size_t* len, size_t* cap,
                                const char* s) {
   for (const char* p = s; *p; ++p) {
      char esc[8];
      int el;
      if (*p == '"' || *p == '\\') {
         esc[0] = '\\';
         esc[1] = *p;
         el = 2;
      } else if ((unsigned char)*p < 0x20) {
         el = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
      } else {
         esc[0] = *p;
         el = 1;
      }
      if (*len + el + 1 > *cap) {
         *cap = (*cap + el + 64) * 2;
         *buf = realloc(*buf, *cap);
      }
      memcpy(*buf + *len, esc, el);
      *len += el;
   }
   (*buf)[*len] = '\0';
}

static char* build_manifest(manifest_rec_t* recs, size_t n, size_t* out_len) {
   size_t cap = 1024;
   size_t len = 0;
   char* buf = malloc(cap);
   if (!buf) return NULL;
   buf[0] = '\0';

#define APP(str)                                        \
   do {                                                 \
      size_t _sl = strlen(str);                         \
      if (len + _sl + 1 > cap) {                        \
         cap = (cap + _sl + 64) * 2;                    \
         buf = realloc(buf, cap);                       \
      }                                                 \
      memcpy(buf + len, str, _sl);                      \
      len += _sl;                                       \
      buf[len] = '\0';                                  \
   } while (0)

   APP("{\n");
   APP("  \"version\": \"2.0\",\n");
   APP("  \"container\": \"batch\",\n");
   APP("  \"created_at\": \"1970-01-01T00:00:00Z\",\n");
   APP("  \"spectra_files\": [\n");
   for (size_t i = 0; i < n; ++i) {
      APP("    {\"entry\": \"");
      json_append_escaped(&buf, &len, &cap, recs[i].entry);
      APP("\", \"original\": \"");
      json_append_escaped(&buf, &len, &cap, recs[i].original);
      APP("\", \"size\": ");
      char num[32];
      snprintf(num, sizeof(num), "%llu", (unsigned long long)recs[i].size);
      APP(num);
      APP("}");
      APP(i + 1 < n ? ",\n" : "\n");
   }
   APP("  ]\n");
   APP("}\n");
#undef APP

   *out_len = len;
   return buf;
}

/* ---------------- default output path ---------------- */

static char* default_output_path(Arguments* a, strlist_t* inputs) {
   /* Single directory input -> "<dirbase>.mszx"; else "batch.mszx". */
   if (a->n_inputs == 1 && is_directory(a->inputs[0])) {
      const char* db = base_name(a->inputs[0]);
      size_t dl = strlen(db);
      /* strip a trailing slash's effect already handled by base_name */
      char* out = malloc(dl + 6);
      if (!out) return NULL;
      snprintf(out, dl + 6, "%s.mszx", db && *db ? db : "batch");
      return out;
   }
   (void)inputs;
   return strdup("batch.mszx");
}

/* ---------------- main entry ---------------- */

int compress_batch(Arguments* arguments) {
   if (!arguments) return -1;

   /* 1) Resolve inputs. */
   strlist_t files = {0};
   int rc = 0;
   for (size_t i = 0; i < arguments->n_inputs && rc == 0; ++i)
      rc = resolve_token(arguments->inputs[i], arguments->recursive, &files);
   if (rc == 0 && arguments->from_file)
      rc = read_paths_from_file(arguments->from_file, arguments->recursive, &files);
   if (rc != 0) {
      strlist_free(&files);
      return -1;
   }

   sort_and_dedup(&files);

   if (files.count == 0) {
      error("batch: no input mzML files matched. Nothing to do.\n");
      strlist_free(&files);
      return -1;
   }

   /* 2) Output path. */
   char* out_path = arguments->output_file;
   int out_path_owned = 0;
   if (!out_path) {
      out_path = default_output_path(arguments, &files);
      out_path_owned = 1;
      if (!out_path) { strlist_free(&files); return -1; }
   }

   /* 3) Open archive WITHOUT O_APPEND (seek-back patch needs real seeks). */
   int out_fd = open_output_file_rw(out_path);
   if (out_fd < 0) {
      error("batch: cannot open output archive '%s'\n", out_path);
      if (out_path_owned) free(out_path);
      strlist_free(&files);
      return -1;
   }
   if (!fd_is_seekable(out_fd)) {
      error("batch: .mszx output must be a regular file, not a pipe/stdout "
            "('%s')\n",
            out_path);
      close_file(out_fd);
      if (out_path_owned) free(out_path);
      strlist_free(&files);
      return -1;
   }

   print("=== Batch compressing %zu mzML file(s) -> %s ===\n", files.count,
         out_path);

   manifest_rec_t* recs = calloc(files.count, sizeof(*recs));
   strlist_t used_names = {0};
   size_t n_ok = 0;
   int had_error = 0;

   /* 4) Stream each file as one tar entry (Option A). */
   for (size_t i = 0; i < files.count; ++i) {
      const char* path = files.items[i];

      int in_fd = open_input_file((char*)path);
      if (in_fd < 0) {
         warning("batch: cannot open '%s'\n", path);
         had_error = 1;
         if (arguments->continue_on_error) continue;
         break;
      }
      void* map = get_mapping(in_fd);
      long fsize = (long)get_filesize((char*)path);
      if (!map || fsize <= 0) {
         warning("batch: cannot map '%s'\n", path);
         if (map) remove_mapping(map, fsize);
         close_file(in_fd);
         had_error = 1;
         if (arguments->continue_on_error) continue;
         break;
      }
      if (!is_mzml(map, (size_t)fsize)) {
         warning("batch: '%s' is not an mzML file (skipped)\n", path);
         remove_mapping(map, fsize);
         close_file(in_fd);
         had_error = 1;
         if (arguments->continue_on_error) continue;
         break;
      }

      char* entry_name = derive_entry_name(path, &used_names);
      if (!entry_name) {
         remove_mapping(map, fsize);
         close_file(in_fd);
         had_error = 1;
         break;
      }

      print("\t[%zu/%zu] %s -> %s\n", i + 1, files.count, base_name(path),
            entry_name);

      tar_entry_t w;
      if (tar_begin_entry(out_fd, entry_name, &w) != 0) {
         error("batch: tar_begin_entry failed for '%s'\n", entry_name);
         free(entry_name);
         remove_mapping(map, fsize);
         close_file(in_fd);
         had_error = 1;
         break;
      }

      int64_t payload_start = get_offset(out_fd);

      data_format_t* df = NULL;
      divisions_t* divs = NULL;
      long bs = arguments->blocksize;
      int fail = 0;
      if (preprocess_mzml(map, fsize, &bs, arguments, &df, &divs)) {
         error("batch: preprocess failed for '%s'\n", path);
         fail = 1;
      } else if (compress_mzml(map, (size_t)fsize, arguments, df, divs,
                               out_fd)) {
         error("batch: compress failed for '%s'\n", path);
         fail = 1;
      }

      int64_t payload_end = get_offset(out_fd);
      uint64_t payload_bytes = (uint64_t)(payload_end - payload_start);

      if (!fail) {
         if (tar_end_entry(out_fd, &w, payload_bytes) != 0) {
            error("batch: tar_end_entry failed for '%s'\n", entry_name);
            fail = 1;
         }
      }

      if (divs) dealloc_divisions(divs);
      if (df) dealloc_df(df);
      remove_mapping(map, fsize);
      close_file(in_fd);

      if (fail) {
         free(entry_name);
         had_error = 1;
         if (arguments->continue_on_error) {
            /* Note: a partially-written entry cannot be cleanly un-appended;
             * fail-fast is the safe default. continue-on-error past a mid-entry
             * failure would corrupt the archive, so we still stop here. */
            error("batch: cannot continue after a mid-entry failure; "
                  "aborting archive.\n");
         }
         break;
      }

      recs[n_ok].entry = entry_name; /* owned */
      recs[n_ok].original = strdup(base_name(path));
      recs[n_ok].size = payload_bytes;
      n_ok++;
   }

   /* 5) Manifest (final entry) + end-of-archive, only if no error. */
   if (!had_error && n_ok > 0) {
      size_t mlen = 0;
      char* manifest = build_manifest(recs, n_ok, &mlen);
      if (!manifest || tar_add_file(out_fd, "manifest.json", manifest, mlen) != 0) {
         error("batch: failed to write manifest.json\n");
         had_error = 1;
      }
      free(manifest);
      if (!had_error && tar_finish(out_fd) != 0) {
         error("batch: failed to finalize archive\n");
         had_error = 1;
      }
   }

   close_file(out_fd);

   /* cleanup manifest records */
   for (size_t i = 0; i < n_ok; ++i) {
      free(recs[i].entry);
      free(recs[i].original);
   }
   free(recs);
   strlist_free(&used_names);
   strlist_free(&files);

   if (had_error) {
      remove_file(out_path);
      if (out_path_owned) free(out_path);
      return -1;
   }

   print("=== Batch wrote %zu entries to %s ===\n", n_ok, out_path);
   if (out_path_owned) free(out_path);
   return 0;
}
