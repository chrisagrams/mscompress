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
#include <windows.h>
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

/* ---------------- directory walk ---------------- */

#ifdef _WIN32
/* Windows directory walk mirroring the POSIX walk_dir: enumerate `dir` via the
 * Win32 FindFirstFile API, recurse into subdirectories when requested, skip
 * "." / "..", and keep regular files whose name matches is_mzml_name. */
static int walk_dir(const char* dir, int recursive, strlist_t* out) {
   char* pattern = join2(dir, "*"); /* "<dir>/*" (Win32 accepts '/' or '\\') */
   if (!pattern) return -1;

   WIN32_FIND_DATAA ffd;
   HANDLE h = FindFirstFileA(pattern, &ffd);
   free(pattern);
   if (h == INVALID_HANDLE_VALUE) {
      warning("batch: cannot open directory '%s'\n", dir);
      return -1;
   }

   int rc = 0;
   do {
      const char* name = ffd.cFileName;
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char* full = join2(dir, name);
      if (!full) { rc = -1; break; }
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
         if (recursive) rc = walk_dir(full, recursive, out);
      } else if (is_mzml_name(full)) {
         if (strlist_push(out, full) != 0) rc = -1;
      }
      free(full);
      if (rc != 0) break;
   } while (FindNextFileA(h, &ffd));

   FindClose(h);
   return rc;
}
#else
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

#ifdef _WIN32
/* Windows internal glob. Win32 wildcards (`*`, `?`) are supported in the LAST
 * path component only; FindFirstFileA matches them natively but returns bare
 * filenames, so the directory prefix is re-joined to rebuild full paths.
 * Recursive `**` is not expanded here — use --recursive with a directory input
 * for that. Mirrors the POSIX glob branch's handling of matched dirs/files. */
static int glob_win32(const char* pattern, int recursive, strlist_t* out) {
   const char* slash = NULL;
   for (const char* p = pattern; *p; ++p)
      if (*p == '/' || *p == '\\') slash = p;

   char dirbuf[MAX_PATH * 4];
   const char* dirpart = ".";
   if (slash) {
      size_t dl = (size_t)(slash - pattern);
      if (dl >= sizeof(dirbuf)) {
         warning("batch: glob pattern too long ('%s')\n", pattern);
         return -1;
      }
      memcpy(dirbuf, pattern, dl);
      dirbuf[dl] = '\0';
      dirpart = dirbuf;
   }

   WIN32_FIND_DATAA ffd;
   HANDLE h = FindFirstFileA(pattern, &ffd);
   if (h == INVALID_HANDLE_VALUE) return 0; /* no match, like GLOB_NOMATCH */

   int rc = 0;
   do {
      const char* name = ffd.cFileName;
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char* full = slash ? join2(dirpart, name) : strdup(name);
      if (!full) { rc = -1; break; }
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
         rc = walk_dir(full, recursive, out);
      } else if (is_mzml_name(full)) {
         if (strlist_push(out, full) != 0) rc = -1;
      }
      free(full);
      if (rc != 0) break;
   } while (FindNextFileA(h, &ffd));

   FindClose(h);
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
      return glob_win32(tok, recursive, out);
#endif
   }

   if (is_directory(tok)) {
      return walk_dir(tok, recursive, out);
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

/* Make `stem`.msz unique within `used`, disambiguating collisions with __N.
 * Pushes the chosen name into `used` and returns a malloc'd copy. */
static char* uniquify_entry_name(const char* stem, strlist_t* used) {
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
   if (strlist_push(used, cand) != 0) return NULL;
   return strdup(cand);
}

/* Derive "<basename minus .mzML>.msz"; disambiguate collisions with __N. */
static char* derive_entry_name(const char* src, strlist_t* used) {
   const char* base = base_name(src);
   size_t bl = strlen(base);
   if (str_iends_with(base, ".mzml")) bl -= 5;
   char stem[512];
   size_t copy = bl < sizeof(stem) - 1 ? bl : sizeof(stem) - 1;
   memcpy(stem, base, copy);
   stem[copy] = '\0';

   return uniquify_entry_name(stem, used);
}

/* Uniquify an explicitly supplied entry name. A caller-provided ".msz" suffix
 * is stripped first so the collision suffix lands before the extension. */
static char* adopt_entry_name(const char* name, strlist_t* used) {
   size_t nl = strlen(name);
   if (str_iends_with(name, ".msz")) nl -= 4;
   char stem[512];
   size_t copy = nl < sizeof(stem) - 1 ? nl : sizeof(stem) - 1;
   memcpy(stem, name, copy);
   stem[copy] = '\0';

   return uniquify_entry_name(stem, used);
}

/* ---------------- manifest JSON (v2) ---------------- */

typedef struct {
   char* filename; /* tar member name of the annotation */
   char* format;   /* reader-supported format tag, e.g. "percolator_tsv" */
   int compressed; /* payload is zstd-compressed (the caller did it) */
   int64_t num_records; /* < 0 = omit */
} manifest_ann_t;

typedef struct {
   char* entry;
   char* original;
   uint64_t size;
   int64_t num_spectra; /* < 0 = omit */
   char* join_key;      /* NULL = omit */
   manifest_ann_t* anns;
   size_t n_anns;
   size_t cap_anns;
} manifest_rec_t;

/* Growable JSON buffer. A single sticky `oom` flag replaces per-append error
 * checks; build_manifest() returns NULL if it is ever set, so an allocation
 * failure can never silently truncate a manifest. */
typedef struct {
   char* buf;
   size_t len;
   size_t cap;
   int oom;
} jbuf_t;

static void jb_reserve(jbuf_t* j, size_t extra) {
   if (j->oom) return;
   if (j->len + extra + 1 <= j->cap) return;
   size_t nc = (j->cap + extra + 64) * 2;
   char* tmp = realloc(j->buf, nc);
   if (!tmp) {
      j->oom = 1;
      return;
   }
   j->buf = tmp;
   j->cap = nc;
}

static void jb_puts(jbuf_t* j, const char* s) {
   size_t sl = strlen(s);
   jb_reserve(j, sl);
   if (j->oom) return;
   memcpy(j->buf + j->len, s, sl);
   j->len += sl;
   j->buf[j->len] = '\0';
}

static void jb_i64(jbuf_t* j, int64_t v) {
   char num[32];
   snprintf(num, sizeof(num), "%lld", (long long)v);
   jb_puts(j, num);
}

/* Append `s` as the body of a JSON string (no surrounding quotes). */
static void jb_escaped(jbuf_t* j, const char* s) {
   for (const char* p = s; *p && !j->oom; ++p) {
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
      jb_reserve(j, (size_t)el);
      if (j->oom) return;
      memcpy(j->buf + j->len, esc, (size_t)el);
      j->len += (size_t)el;
      j->buf[j->len] = '\0';
   }
}

/* Append `"key": "value"` with the value escaped. */
static void jb_kv_str(jbuf_t* j, const char* key, const char* value) {
   jb_puts(j, "\"");
   jb_puts(j, key);
   jb_puts(j, "\": \"");
   jb_escaped(j, value);
   jb_puts(j, "\"");
}

/* Build the v2 manifest. Field order is fixed so identical inputs always
 * produce byte-identical archives (there are no timestamps by design).
 * Optional fields are omitted entirely rather than emitted as null, so a
 * reader written against the original v2 schema still parses the result. */
static char* build_manifest(manifest_rec_t* recs, size_t n,
                            const char* description, const char* extra_json,
                            size_t* out_len) {
   jbuf_t j = {0};
   jb_reserve(&j, 1024);
   if (j.oom) return NULL;
   j.buf[0] = '\0';

   jb_puts(&j, "{\n");
   jb_puts(&j, "  \"version\": \"2.0\",\n");
   jb_puts(&j, "  \"container\": \"batch\",\n");
   if (description) {
      jb_puts(&j, "  ");
      jb_kv_str(&j, "description", description);
      jb_puts(&j, ",\n");
   }
   if (extra_json) {
      jb_puts(&j, "  \"extra\": ");
      jb_puts(&j, extra_json);
      jb_puts(&j, ",\n");
   }
   jb_puts(&j, "  \"spectra_files\": [\n");
   for (size_t i = 0; i < n; ++i) {
      manifest_rec_t* r = &recs[i];
      jb_puts(&j, "    {");
      jb_kv_str(&j, "entry", r->entry);
      jb_puts(&j, ", ");
      jb_kv_str(&j, "original", r->original);
      jb_puts(&j, ", \"size\": ");
      jb_i64(&j, (int64_t)r->size);
      if (r->num_spectra >= 0) {
         jb_puts(&j, ", \"num_spectra\": ");
         jb_i64(&j, r->num_spectra);
      }
      if (r->join_key) {
         jb_puts(&j, ", ");
         jb_kv_str(&j, "join_key", r->join_key);
      }
      if (r->n_anns > 0) {
         jb_puts(&j, ", \"annotations\": [");
         for (size_t k = 0; k < r->n_anns; ++k) {
            manifest_ann_t* a = &r->anns[k];
            jb_puts(&j, "{");
            jb_kv_str(&j, "filename", a->filename);
            jb_puts(&j, ", ");
            jb_kv_str(&j, "format", a->format);
            jb_puts(&j, ", \"compressed\": ");
            jb_puts(&j, a->compressed ? "true" : "false");
            if (a->num_records >= 0) {
               jb_puts(&j, ", \"num_records\": ");
               jb_i64(&j, a->num_records);
            }
            jb_puts(&j, "}");
            if (k + 1 < r->n_anns) jb_puts(&j, ", ");
         }
         jb_puts(&j, "]");
      }
      jb_puts(&j, "}");
      jb_puts(&j, i + 1 < n ? ",\n" : "\n");
   }
   jb_puts(&j, "  ]\n");
   jb_puts(&j, "}\n");

   if (j.oom) {
      free(j.buf);
      return NULL;
   }
   *out_len = j.len;
   return j.buf;
}

static void free_manifest_rec(manifest_rec_t* r) {
   for (size_t k = 0; k < r->n_anns; ++k) {
      free(r->anns[k].filename);
      free(r->anns[k].format);
   }
   free(r->anns);
   free(r->entry);
   free(r->original);
   free(r->join_key);
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


/* ---------------- incremental writer ---------------- */

struct batch_writer {
   int out_fd;
   char* out_path;
   manifest_rec_t* recs;
   size_t n_recs;
   size_t cap_recs;
   strlist_t used_names;
   char* description;
   char* extra_json;
   /* Set when an entry failed after its header was written. A half-written
    * entry cannot be un-appended, so the archive is unrecoverable and
    * finish() must refuse rather than emit a corrupt manifest. */
   int poisoned;
};

static void batch_writer_free(batch_writer_t* w) {
   if (!w) return;
   for (size_t i = 0; i < w->n_recs; ++i) free_manifest_rec(&w->recs[i]);
   free(w->recs);
   strlist_free(&w->used_names);
   free(w->description);
   free(w->extra_json);
   free(w->out_path);
   free(w);
}

batch_writer_t* batch_writer_open(const char* out_path) {
   if (!out_path) return NULL;

   batch_writer_t* w = calloc(1, sizeof(*w));
   if (!w) return NULL;

   w->out_path = strdup(out_path);
   if (!w->out_path) {
      free(w);
      return NULL;
   }

   /* Opened read-write WITHOUT O_APPEND: tar_end_entry seeks back to patch the
    * header, and under O_APPEND every write would be forced to EOF instead. */
   w->out_fd = open_output_file_rw(w->out_path);
   if (w->out_fd < 0) {
      error("batch: cannot open output archive '%s'\n", out_path);
      free(w->out_path);
      free(w);
      return NULL;
   }
   if (!fd_is_seekable(w->out_fd)) {
      error("batch: .mszx output must be a regular file, not a pipe/stdout "
            "('%s')\n",
            out_path);
      close_file(w->out_fd);
      free(w->out_path);
      free(w);
      return NULL;
   }
   return w;
}

static manifest_rec_t* batch_writer_push_rec(batch_writer_t* w) {
   if (w->n_recs == w->cap_recs) {
      size_t nc = w->cap_recs ? w->cap_recs * 2 : 16;
      manifest_rec_t* tmp = realloc(w->recs, nc * sizeof(*tmp));
      if (!tmp) return NULL;
      w->recs = tmp;
      w->cap_recs = nc;
   }
   manifest_rec_t* r = &w->recs[w->n_recs];
   memset(r, 0, sizeof(*r));
   r->num_spectra = -1;
   return r;
}

int batch_writer_add_mzml(batch_writer_t* w, const char* entry_name,
                          const char* src_name, void* mapping, size_t filesize,
                          Arguments* args) {
   if (!w || !mapping || !args || filesize == 0) return -1;
   if (w->poisoned) {
      error("batch: writer is unusable after a mid-entry failure\n");
      return -1;
   }

   const char* label = src_name ? src_name : "(unnamed)";

   if (!is_mzml(mapping, filesize)) {
      warning("batch: '%s' is not an mzML file (skipped)\n", label);
      return -1;
   }

   /* The writer owns entry naming so the CLI and every binding agree. */
   char* name = entry_name ? adopt_entry_name(entry_name, &w->used_names)
                           : derive_entry_name(label, &w->used_names);
   if (!name) return -1;

   tar_entry_t te;
   if (tar_begin_entry(w->out_fd, name, &te) != 0) {
      error("batch: tar_begin_entry failed for '%s'\n", name);
      free(name);
      return -1;
   }

   int64_t payload_start = get_offset(w->out_fd);

   data_format_t* df = NULL;
   divisions_t* divs = NULL;
   /* Local copy: blocksize auto-tuning inside preprocess_mzml must not leak
    * from one entry into the next. */
   long bs = args->blocksize;
   int fail = 0;

   if (preprocess_mzml((char*)mapping, (long)filesize, &bs, args, &df, &divs)) {
      error("batch: preprocess failed for '%s'\n", label);
      fail = 1;
   } else if (compress_mzml((char*)mapping, filesize, args, df, divs,
                            w->out_fd)) {
      error("batch: compress failed for '%s'\n", label);
      fail = 1;
   }

   int64_t num_spectra = 0;
   if (!fail && divs) {
      for (int i = 0; i < divs->n_divisions; ++i) {
         if (divs->divisions[i] && divs->divisions[i]->spectra)
            num_spectra += divs->divisions[i]->spectra->total_spec;
      }
   }

   int64_t payload_end = get_offset(w->out_fd);
   uint64_t payload_bytes = (uint64_t)(payload_end - payload_start);

   if (!fail && tar_end_entry(w->out_fd, &te, payload_bytes) != 0) {
      error("batch: tar_end_entry failed for '%s'\n", name);
      fail = 1;
   }

   if (divs) dealloc_divisions(divs);
   if (df) dealloc_df(df);

   if (fail) {
      /* Bytes are already on disk under this entry's header. */
      w->poisoned = 1;
      free(name);
      return -1;
   }

   manifest_rec_t* r = batch_writer_push_rec(w);
   if (!r) {
      w->poisoned = 1;
      free(name);
      return -1;
   }
   r->entry = name; /* ownership transferred */
   r->original = strdup(base_name(label));
   r->size = payload_bytes;
   r->num_spectra = num_spectra;
   if (!r->original) {
      free_manifest_rec(r);
      w->poisoned = 1;
      return -1;
   }
   w->n_recs++;
   return (int)(w->n_recs - 1);
}

int batch_writer_add_annotation(batch_writer_t* w, int entry_index,
                                const char* archive_name, const void* data,
                                size_t len, const char* format, int compressed,
                                int64_t num_records) {
   if (!w || !archive_name || (!data && len > 0) || !format) return -1;
   if (w->poisoned) return -1;
   if (entry_index < 0 || (size_t)entry_index >= w->n_recs) {
      error("batch: annotation refers to unknown entry index %d\n", entry_index);
      return -1;
   }

   manifest_rec_t* r = &w->recs[entry_index];

   if (r->n_anns == r->cap_anns) {
      size_t nc = r->cap_anns ? r->cap_anns * 2 : 4;
      manifest_ann_t* tmp = realloc(r->anns, nc * sizeof(*tmp));
      if (!tmp) return -1;
      r->anns = tmp;
      r->cap_anns = nc;
   }

   /* Known size up front, so this is a plain single-shot tar member. */
   if (tar_add_file(w->out_fd, archive_name, data, len) != 0) {
      error("batch: failed to write annotation '%s'\n", archive_name);
      w->poisoned = 1;
      return -1;
   }

   manifest_ann_t* a = &r->anns[r->n_anns];
   memset(a, 0, sizeof(*a));
   a->filename = strdup(archive_name);
   a->format = strdup(format);
   a->compressed = compressed ? 1 : 0;
   a->num_records = num_records;
   if (!a->filename || !a->format) {
      free(a->filename);
      free(a->format);
      return -1;
   }
   r->n_anns++;
   return 0;
}

int batch_writer_set_join_key(batch_writer_t* w, int entry_index,
                              const char* join_key) {
   if (!w || !join_key) return -1;
   if (entry_index < 0 || (size_t)entry_index >= w->n_recs) return -1;
   char* dup = strdup(join_key);
   if (!dup) return -1;
   free(w->recs[entry_index].join_key);
   w->recs[entry_index].join_key = dup;
   return 0;
}

int batch_writer_set_description(batch_writer_t* w, const char* description) {
   if (!w || !description) return -1;
   char* dup = strdup(description);
   if (!dup) return -1;
   free(w->description);
   w->description = dup;
   return 0;
}

int batch_writer_set_extra_json(batch_writer_t* w, const char* extra_json) {
   if (!w || !extra_json) return -1;
   char* dup = strdup(extra_json);
   if (!dup) return -1;
   free(w->extra_json);
   w->extra_json = dup;
   return 0;
}

int batch_writer_finish(batch_writer_t* w) {
   if (!w) return -1;

   if (w->poisoned) {
      error("batch: refusing to finalize an archive with a failed entry\n");
      batch_writer_abort(w);
      return -1;
   }
   if (w->n_recs == 0) {
      error("batch: no entries were written; nothing to finalize\n");
      batch_writer_abort(w);
      return -1;
   }

   int rc = 0;
   size_t mlen = 0;
   char* manifest =
       build_manifest(w->recs, w->n_recs, w->description, w->extra_json, &mlen);

   /* manifest.json is written LAST: entry sizes are only known once each
    * payload has streamed out. */
   if (!manifest || tar_add_file(w->out_fd, "manifest.json", manifest, mlen)) {
      error("batch: failed to write manifest.json\n");
      rc = -1;
   }
   free(manifest);

   if (rc == 0 && tar_finish(w->out_fd) != 0) {
      error("batch: failed to finalize archive\n");
      rc = -1;
   }

   close_file(w->out_fd);
   w->out_fd = -1;

   if (rc != 0) remove_file(w->out_path);

   batch_writer_free(w);
   return rc;
}

void batch_writer_abort(batch_writer_t* w) {
   if (!w) return;
   if (w->out_fd >= 0) close_file(w->out_fd);
   if (w->out_path) remove_file(w->out_path);
   batch_writer_free(w);
}

/* ---------------- CLI driver ---------------- */

int compress_batch(Arguments* arguments) {
   if (!arguments) return -1;

   /* 1) Resolve inputs. */
   strlist_t files = {0};
   int rc = 0;
   for (size_t i = 0; i < arguments->n_inputs && rc == 0; ++i)
      rc = resolve_token(arguments->inputs[i], arguments->recursive, &files);
   if (rc == 0 && arguments->from_file)
      rc = read_paths_from_file(arguments->from_file, arguments->recursive,
                                &files);
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
      if (!out_path) {
         strlist_free(&files);
         return -1;
      }
   }

   batch_writer_t* w = batch_writer_open(out_path);
   if (!w) {
      if (out_path_owned) free(out_path);
      strlist_free(&files);
      return -1;
   }

   print("=== Batch compressing %zu mzML file(s) -> %s ===\n", files.count,
         out_path);

   /* 3) Stream each file as one tar entry. */
   size_t n_ok = 0, n_skipped = 0;
   int fatal = 0;

   for (size_t i = 0; i < files.count; ++i) {
      const char* path = files.items[i];

      int in_fd = open_input_file((char*)path);
      if (in_fd < 0) {
         warning("batch: cannot open '%s'\n", path);
         n_skipped++;
         if (arguments->continue_on_error) continue;
         fatal = 1;
         break;
      }

      void* map = get_mapping(in_fd);
      long fsize = (long)get_filesize((char*)path);
      if (!map || fsize <= 0) {
         warning("batch: cannot map '%s'\n", path);
         if (map) remove_mapping(map, fsize);
         close_file(in_fd);
         n_skipped++;
         if (arguments->continue_on_error) continue;
         fatal = 1;
         break;
      }

      print("\t[%zu/%zu] %s\n", i + 1, files.count, base_name(path));

      int idx = batch_writer_add_mzml(w, NULL, path, map, (size_t)fsize,
                                      arguments);

      remove_mapping(map, fsize);
      close_file(in_fd);

      if (idx < 0) {
         /* A mid-entry failure corrupts the archive, so --continue-on-error
          * cannot rescue it; only pre-entry rejections (a non-mzML input) are
          * skippable. */
         if (w->poisoned) {
            error("batch: cannot continue after a mid-entry failure; "
                  "aborting archive.\n");
            fatal = 1;
            break;
         }
         n_skipped++;
         if (arguments->continue_on_error) continue;
         fatal = 1;
         break;
      }
      n_ok++;
   }

   if (fatal || n_ok == 0) {
      if (n_ok == 0 && !fatal)
         error("batch: no input mzML files could be compressed.\n");
      batch_writer_abort(w);
      if (out_path_owned) free(out_path);
      strlist_free(&files);
      return -1;
   }

   /* 4) Manifest + end-of-archive. */
   int frc = batch_writer_finish(w);

   if (frc == 0) {
      print("=== Batch wrote %zu entries to %s ===\n", n_ok, out_path);
      if (n_skipped)
         warning("batch: skipped %zu input(s); see warnings above.\n",
                 n_skipped);
   }

   if (out_path_owned) free(out_path);
   strlist_free(&files);
   return frc;
}
