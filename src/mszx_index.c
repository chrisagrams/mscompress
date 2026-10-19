/**
 * @file mszx_index.c
 * @brief Global per-spectrum index for batch (v2) .mszx archives.
 *
 * The index is one tar member, `index.mszxi`, holding fixed-width columns (one
 * row per spectrum, ordered by (entry, spectrum_idx)) plus a sorted key for
 * selected columns. A range query on a sorted key is a few binary searches;
 * every other column is filtered as a vector. All fields are little-endian
 * and every section starts on an 8-byte boundary, so readers can view the
 * member through typed arrays without parsing (tar members themselves start
 * on 512-byte boundaries).
 *
 * Layout (offsets are relative to the start of the member):
 *
 *   Header (32 bytes)
 *     0   char[4] magic "MSZI"
 *     4   u16     format version (MSZX_INDEX_FORMAT_VERSION)
 *     6   u16     flags (0)
 *     8   u64     n_rows
 *     16  u32     n_entries   (spectra files in the archive)
 *     20  u32     n_columns
 *     24  u32     n_sorted_keys
 *     28  u32     reserved (0)
 *   Entry offsets, (n_entries + 1) x u64
 *     entry e owns rows [offsets[e], offsets[e+1]); offsets[n_entries] is
 *     n_rows. The `entry` and `spectrum_idx` columns are not stored: a row's
 *     entry is the last e with offsets[e] <= row, and its spectrum_idx is
 *     row - offsets[entry].
 *   Column directory, n_columns x 24 bytes
 *     u32 column id, u32 dtype, u64 offset, u64 byte_len
 *   Sorted-key directory, n_sorted_keys x 24 bytes
 *     u32 column id, u32 kind, u64 n_valid, u64 perm offset
 *   Sections (8-byte aligned, zero padded)
 *     each column: n_rows values of its dtype
 *     each MSZX_KEY_PERM key: u32 perm[n_valid]
 *
 * A sorted key is one of two kinds, and always reads values from its column:
 *   MSZX_KEY_PERM  perm lists the rows whose value is not NaN, ordered by
 *                  (value, row); a range is two binary searches over it.
 *   MSZX_KEY_RUNS  the column has no NaN and never decreases within an entry
 *                  (e.g. retention time), so no perm is stored (n_valid =
 *                  n_rows, perm offset 0); a range is two binary searches per
 *                  entry, and its hits are already in row order.
 * The writer picks MSZX_KEY_RUNS whenever the data allows it.
 *
 * Readers skip column ids, dtypes and key kinds they do not know, and locate
 * every section through the directories, so new columns and sections can be
 * added without a version bump.
 *
 * Missing values are NaN for float columns and 0 for integer ones. Ties keep
 * row order, so the output is byte-reproducible.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"

#define IDX_MAGIC "MSZI"
#define IDX_HEADER_SIZE 32
#define IDX_COLDIR_SIZE 24
#define IDX_KEYDIR_SIZE 24

/* ---------------- column catalogue ---------------- */

typedef struct {
   int id;
   const char* name;
   int dtype; /* 0: derived from the entry offsets, not stored */
} column_def_t;

/* Column order here is the order they are written in. m/z values are f32:
 * at m/z 2000 that is ~0.12 mDa (0.06 ppm), finer than instruments measure. */
static const column_def_t COLUMNS[] = {
    {MSZX_COL_ENTRY, "entry", 0},
    {MSZX_COL_SPECTRUM_IDX, "spectrum_idx", 0},
    {MSZX_COL_SCAN, "scan", MSZX_DT_U32},
    {MSZX_COL_MS_LEVEL, "ms_level", MSZX_DT_U8},
    {MSZX_COL_RT_SEC, "rt_sec", MSZX_DT_F32},
    {MSZX_COL_PRECURSOR_MZ, "precursor_mz", MSZX_DT_F32},
    {MSZX_COL_CHARGE, "charge", MSZX_DT_I8},
    {MSZX_COL_TIC, "tic", MSZX_DT_F32},
    {MSZX_COL_BASE_PEAK_MZ, "base_peak_mz", MSZX_DT_F32},
};
#define N_COLUMNS (sizeof(COLUMNS) / sizeof(COLUMNS[0]))

static const int SORTED_KEYS[] = {MSZX_COL_RT_SEC, MSZX_COL_PRECURSOR_MZ};
#define N_SORTED_KEYS (sizeof(SORTED_KEYS) / sizeof(SORTED_KEYS[0]))

static size_t dtype_size(int dtype) {
   switch (dtype) {
      case MSZX_DT_U8:
      case MSZX_DT_I8:
         return 1;
      case MSZX_DT_U16:
      case MSZX_DT_I16:
         return 2;
      case MSZX_DT_U32:
      case MSZX_DT_I32:
      case MSZX_DT_F32:
         return 4;
      case MSZX_DT_U64:
      case MSZX_DT_I64:
      case MSZX_DT_F64:
         return 8;
      default:
         return 0;
   }
}

const char* mszx_index_column_name(int id) {
   for (size_t i = 0; i < N_COLUMNS; ++i)
      if (COLUMNS[i].id == id) return COLUMNS[i].name;
   return NULL;
}

int mszx_index_column_id(const char* name) {
   if (!name) return -1;
   for (size_t i = 0; i < N_COLUMNS; ++i)
      if (strcmp(COLUMNS[i].name, name) == 0) return COLUMNS[i].id;
   /* Short aliases accepted by --query. */
   if (strcmp(name, "rt") == 0) return MSZX_COL_RT_SEC;
   if (strcmp(name, "mz") == 0 || strcmp(name, "precursor") == 0)
      return MSZX_COL_PRECURSOR_MZ;
   if (strcmp(name, "level") == 0) return MSZX_COL_MS_LEVEL;
   return -1;
}

size_t mszx_index_columns(const char*** names) {
   static const char* list[N_COLUMNS];
   for (size_t i = 0; i < N_COLUMNS; ++i) list[i] = COLUMNS[i].name;
   if (names) *names = list;
   return N_COLUMNS;
}

size_t mszx_index_sorted_keys(const char*** names) {
   static const char* list[N_SORTED_KEYS];
   for (size_t i = 0; i < N_SORTED_KEYS; ++i)
      list[i] = mszx_index_column_name(SORTED_KEYS[i]);
   if (names) *names = list;
   return N_SORTED_KEYS;
}

/* ---------------- little-endian helpers ---------------- */
/* The msz format already assumes a little-endian host; these just keep the
 * index layout explicit and free of alignment assumptions while writing. */

static void put_u16(char* p, uint16_t v) { memcpy(p, &v, 2); }
static void put_u32(char* p, uint32_t v) { memcpy(p, &v, 4); }
static void put_u64(char* p, uint64_t v) { memcpy(p, &v, 8); }
static uint16_t get_u16(const char* p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t get_u32(const char* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t get_u64(const char* p) { uint64_t v; memcpy(&v, p, 8); return v; }

static size_t align8(size_t x) { return (x + 7) & ~(size_t)7; }

/* ---------------- per-spectrum metadata extraction ---------------- */

typedef struct {
   uint32_t entry; /* only used to build the entry offsets */
   uint32_t scan;
   uint8_t ms_level;
   float rt_sec;
   float precursor_mz;
   int8_t charge;
   float tic;
   float base_peak_mz;
} index_row_t;

static const char* find_bytes(const char* hay, size_t hay_len, const char* needle) {
   size_t nl = strlen(needle);
   if (nl == 0 || hay_len < nl) return NULL;
   const char* end = hay + hay_len - nl;
   for (const char* p = hay; p <= end; ++p) {
      p = memchr(p, needle[0], (size_t)(end - p) + 1);
      if (!p) return NULL;
      if (memcmp(p, needle, nl) == 0) return p;
   }
   return NULL;
}

/* Locate the first cvParam in [s, s+len) with `accession` and copy its value
 * attribute into `val` (NUL-terminated). The attribute search is confined to
 * that one tag, so a cvParam without a value never borrows the next one's.
 * Optionally returns the tag's bounds for a unit lookup. */
static int cv_value(const char* s, size_t len, const char* accession,
                    char* val, size_t val_cap, const char** tag_start,
                    size_t* tag_len) {
   char needle[48];
   snprintf(needle, sizeof(needle), "accession=\"%s\"", accession);
   const char* hit = find_bytes(s, len, needle);
   if (!hit) return 0;

   const char* start = hit;
   while (start > s && *start != '<') start--;
   const char* end = memchr(hit, '>', len - (size_t)(hit - s));
   if (!end) return 0;
   size_t tl = (size_t)(end - start);

   const char* v = find_bytes(start, tl, "value=\"");
   if (!v) return 0;
   v += 7;
   const char* q = memchr(v, '"', (size_t)(end - v));
   if (!q) return 0;
   size_t vl = (size_t)(q - v);
   if (vl == 0 || vl >= val_cap) return 0;
   memcpy(val, v, vl);
   val[vl] = '\0';

   if (tag_start) *tag_start = start;
   if (tag_len) *tag_len = tl;
   return 1;
}

static double cv_double(const char* s, size_t len, const char* accession) {
   char buf[64];
   if (!cv_value(s, len, accession, buf, sizeof(buf), NULL, NULL)) return NAN;
   char* e;
   double v = strtod(buf, &e);
   return e == buf ? NAN : v;
}

/* Retention time in seconds; NaN when absent. */
static double cv_rt_seconds(const char* s, size_t len) {
   char buf[64];
   const char* tag;
   size_t tl;
   if (!cv_value(s, len, "MS:1000016", buf, sizeof(buf), &tag, &tl)) return NAN;
   char* e;
   double v = strtod(buf, &e);
   if (e == buf) return NAN;
   if (find_bytes(tag, tl, "unitAccession=\"UO:0000031\"")) return v * 60.0;
   if (find_bytes(tag, tl, "unitAccession=\"UO:0000032\"")) return v * 3600.0;
   if (find_bytes(tag, tl, "unitAccession=\"UO:0000028\"")) return v / 1000.0;
   return v; /* UO:0000010 (second) or unitless */
}

static void extract_row(const char* spec, size_t len, index_row_t* r) {
   r->rt_sec = (float)cv_rt_seconds(spec, len);
   r->precursor_mz = (float)cv_double(spec, len, "MS:1000744");
   double z = cv_double(spec, len, "MS:1000041");
   r->charge = (isnan(z) || z < -128 || z > 127) ? 0 : (int8_t)z;
   r->tic = (float)cv_double(spec, len, "MS:1000285");
   r->base_peak_mz = (float)cv_double(spec, len, "MS:1000504");
}

/* ---------------- builder ---------------- */

struct mszx_index_builder {
   index_row_t* rows;
   size_t n;
   size_t cap;
};

mszx_index_builder_t* mszx_index_builder_new(void) {
   return calloc(1, sizeof(mszx_index_builder_t));
}

void mszx_index_builder_free(mszx_index_builder_t* b) {
   if (!b) return;
   free(b->rows);
   free(b);
}

size_t mszx_index_builder_rows(const mszx_index_builder_t* b) {
   return b ? b->n : 0;
}

int mszx_index_builder_add(mszx_index_builder_t* b, uint32_t entry,
                           const char* map, size_t len, divisions_t* divs) {
   if (!b || !map || !divs) return -1;

   for (int d = 0; d < divs->n_divisions; ++d) {
      division_t* div = divs->divisions[d];
      if (!div || !div->spectra) continue;
      int n = div->spectra->total_spec;
      for (int j = 0; j < n; ++j) {
         if (b->n == b->cap) {
            size_t nc = b->cap ? b->cap * 2 : 1024;
            index_row_t* tmp = realloc(b->rows, nc * sizeof(*tmp));
            if (!tmp) return -1;
            b->rows = tmp;
            b->cap = nc;
         }
         if (b->n >= UINT32_MAX) {
            error("mszx index: more than %u spectra are not supported\n",
                  UINT32_MAX);
            return -1;
         }

         uint64_t start = div->spectra->start_positions[j];
         uint64_t end = div->spectra->end_positions[j];
         /* Spectrum-level metadata precedes the binaryDataArrayList, so stop
          * at the first binary payload rather than scanning the base64. */
         if (div->mz && j < div->mz->total_spec &&
             div->mz->start_positions[j] > start &&
             div->mz->start_positions[j] < end)
            end = div->mz->start_positions[j];
         if (start > len || end > len || end < start) return -1;

         index_row_t* r = &b->rows[b->n];
         memset(r, 0, sizeof(*r));
         r->entry = entry;
         r->scan = div->scans ? div->scans[j] : 0;
         r->ms_level = div->ms_levels
                           ? (uint8_t)(div->ms_levels[j] > 255 ? 255
                                                               : div->ms_levels[j])
                           : 0;
         extract_row(map + start, (size_t)(end - start), r);
         b->n++;
      }
   }
   return 0;
}

typedef struct {
   double key;
   uint32_t row;
} sort_item_t;

static int cmp_sort_item(const void* a, const void* b) {
   const sort_item_t* x = (const sort_item_t*)a;
   const sort_item_t* y = (const sort_item_t*)b;
   if (x->key < y->key) return -1;
   if (x->key > y->key) return 1;
   return x->row < y->row ? -1 : (x->row > y->row);
}

static void write_column(char* dst, int id, const index_row_t* rows, size_t n) {
   for (size_t i = 0; i < n; ++i) {
      const index_row_t* r = &rows[i];
      switch (id) {
         case MSZX_COL_SCAN: memcpy(dst + i * 4, &r->scan, 4); break;
         case MSZX_COL_MS_LEVEL: memcpy(dst + i, &r->ms_level, 1); break;
         case MSZX_COL_RT_SEC: memcpy(dst + i * 4, &r->rt_sec, 4); break;
         case MSZX_COL_PRECURSOR_MZ: memcpy(dst + i * 4, &r->precursor_mz, 4); break;
         case MSZX_COL_CHARGE: memcpy(dst + i, &r->charge, 1); break;
         case MSZX_COL_TIC: memcpy(dst + i * 4, &r->tic, 4); break;
         case MSZX_COL_BASE_PEAK_MZ: memcpy(dst + i * 4, &r->base_peak_mz, 4); break;
      }
   }
}

static double row_value(const index_row_t* r, int id) {
   switch (id) {
      case MSZX_COL_RT_SEC: return r->rt_sec;
      case MSZX_COL_PRECURSOR_MZ: return r->precursor_mz;
      case MSZX_COL_TIC: return r->tic;
      case MSZX_COL_BASE_PEAK_MZ: return r->base_peak_mz;
      default: return NAN;
   }
}

/* Whether `id` has no NaN and never decreases within an entry. */
static int runs_sorted(const index_row_t* rows, size_t n, int id) {
   for (size_t i = 0; i < n; ++i) {
      double v = row_value(&rows[i], id);
      if (isnan(v)) return 0;
      if (i > 0 && rows[i].entry == rows[i - 1].entry &&
          v < row_value(&rows[i - 1], id))
         return 0;
   }
   return 1;
}

void* mszx_index_builder_serialize(const mszx_index_builder_t* b,
                                   uint32_t n_entries, size_t* out_len) {
   if (!b || !out_len) return NULL;
   size_t n = b->n;
   const index_row_t* rows = b->rows;

   for (size_t i = 0; i < n; ++i) {
      if (rows[i].entry >= n_entries ||
          (i > 0 && rows[i].entry < rows[i - 1].entry)) {
         error("mszx index: rows are not grouped by entry\n");
         return NULL;
      }
   }

   size_t n_stored = 0;
   for (size_t i = 0; i < N_COLUMNS; ++i) n_stored += COLUMNS[i].dtype != 0;

   /* Pick each key's kind and count its perm before laying anything out. */
   int key_kind[N_SORTED_KEYS];
   size_t key_valid[N_SORTED_KEYS];
   for (size_t k = 0; k < N_SORTED_KEYS; ++k) {
      key_kind[k] = runs_sorted(rows, n, SORTED_KEYS[k]) ? MSZX_KEY_RUNS
                                                         : MSZX_KEY_PERM;
      key_valid[k] = 0;
      for (size_t i = 0; i < n; ++i)
         if (!isnan(row_value(&rows[i], SORTED_KEYS[k]))) key_valid[k]++;
   }

   /* Lay out every section first, then fill one zeroed buffer. */
   size_t entries_off = IDX_HEADER_SIZE;
   size_t off = entries_off + ((size_t)n_entries + 1) * 8 +
                n_stored * IDX_COLDIR_SIZE + N_SORTED_KEYS * IDX_KEYDIR_SIZE;
   off = align8(off);
   size_t col_off[N_COLUMNS];
   for (size_t i = 0; i < N_COLUMNS; ++i) {
      if (!COLUMNS[i].dtype) continue;
      col_off[i] = off;
      off = align8(off + n * dtype_size(COLUMNS[i].dtype));
   }
   size_t perm_off[N_SORTED_KEYS];
   for (size_t k = 0; k < N_SORTED_KEYS; ++k) {
      perm_off[k] = 0;
      if (key_kind[k] != MSZX_KEY_PERM) continue;
      perm_off[k] = off;
      off = align8(off + key_valid[k] * 4);
   }
   size_t total = off;

   char* buf = calloc(1, total);
   if (!buf) return NULL;

   memcpy(buf, IDX_MAGIC, 4);
   put_u16(buf + 4, MSZX_INDEX_FORMAT_VERSION);
   put_u16(buf + 6, 0);
   put_u64(buf + 8, (uint64_t)n);
   put_u32(buf + 16, n_entries);
   put_u32(buf + 20, (uint32_t)n_stored);
   put_u32(buf + 24, (uint32_t)N_SORTED_KEYS);
   put_u32(buf + 28, 0);

   /* offsets[e] = first row of entry e (rows are grouped by entry). */
   size_t row = 0;
   for (uint32_t e = 0; e <= n_entries; ++e) {
      while (row < n && rows[row].entry < e) row++;
      put_u64(buf + entries_off + (size_t)e * 8, (uint64_t)row);
   }

   char* dir = buf + entries_off + ((size_t)n_entries + 1) * 8;
   for (size_t i = 0; i < N_COLUMNS; ++i) {
      if (!COLUMNS[i].dtype) continue;
      put_u32(dir, (uint32_t)COLUMNS[i].id);
      put_u32(dir + 4, (uint32_t)COLUMNS[i].dtype);
      put_u64(dir + 8, (uint64_t)col_off[i]);
      put_u64(dir + 16, (uint64_t)(n * dtype_size(COLUMNS[i].dtype)));
      dir += IDX_COLDIR_SIZE;
      write_column(buf + col_off[i], COLUMNS[i].id, rows, n);
   }

   sort_item_t* items = NULL;
   for (size_t k = 0; k < N_SORTED_KEYS; ++k) {
      int id = SORTED_KEYS[k];
      put_u32(dir, (uint32_t)id);
      put_u32(dir + 4, (uint32_t)key_kind[k]);
      put_u64(dir + 8, key_kind[k] == MSZX_KEY_PERM ? key_valid[k] : n);
      put_u64(dir + 16, (uint64_t)perm_off[k]);
      dir += IDX_KEYDIR_SIZE;
      if (key_kind[k] != MSZX_KEY_PERM || key_valid[k] == 0) continue;

      if (!items && !(items = malloc(n * sizeof(*items)))) {
         free(buf);
         return NULL;
      }
      size_t m = 0;
      for (size_t i = 0; i < n; ++i) {
         double v = row_value(&rows[i], id);
         if (isnan(v)) continue;
         items[m].key = v;
         items[m].row = (uint32_t)i;
         m++;
      }
      qsort(items, m, sizeof(*items), cmp_sort_item);
      for (size_t i = 0; i < m; ++i)
         put_u32(buf + perm_off[k] + i * 4, items[i].row);
   }
   free(items);

   *out_len = total;
   return buf;
}

/* ---------------- reader ---------------- */

#define MAX_COLUMN_ID 64

typedef struct {
   const char* data; /* NULL when the column is absent */
   int dtype;
} col_view_t;

typedef struct {
   int kind;             /* 0 when the column has no sorted key */
   const uint32_t* perm; /* MSZX_KEY_PERM only */
   uint64_t n_valid;
} key_view_t;

struct mszx_index {
   const char* base;
   size_t len;
   uint64_t n_rows;
   uint32_t n_entries;
   const uint64_t* offsets; /* n_entries + 1 */
   col_view_t cols[MAX_COLUMN_ID];
   key_view_t keys[MAX_COLUMN_ID];
};

static int range_ok(size_t len, uint64_t off, uint64_t bytes) {
   return off <= len && bytes <= len - off;
}

static int is_derived(int column) {
   return column == MSZX_COL_ENTRY || column == MSZX_COL_SPECTRUM_IDX;
}

mszx_index_t* mszx_index_open(const void* data, size_t len) {
   const char* p = (const char*)data;
   if (!p || len < IDX_HEADER_SIZE || memcmp(p, IDX_MAGIC, 4) != 0) {
      warning("mszx index: bad magic\n");
      return NULL;
   }
   if (get_u16(p + 4) != MSZX_INDEX_FORMAT_VERSION) {
      warning("mszx index: unsupported format version %u\n", get_u16(p + 4));
      return NULL;
   }
   uint64_t n_rows = get_u64(p + 8);
   uint32_t n_entries = get_u32(p + 16);
   uint32_t n_cols = get_u32(p + 20);
   uint32_t n_keys = get_u32(p + 24);
   uint64_t entries_bytes = ((uint64_t)n_entries + 1) * 8;
   uint64_t dir_bytes = entries_bytes + (uint64_t)n_cols * IDX_COLDIR_SIZE +
                        (uint64_t)n_keys * IDX_KEYDIR_SIZE;
   if (n_rows > UINT32_MAX || !range_ok(len, IDX_HEADER_SIZE, dir_bytes)) {
      warning("mszx index: corrupt header\n");
      return NULL;
   }

   const uint64_t* offsets = (const uint64_t*)(p + IDX_HEADER_SIZE);
   int ok = offsets[0] == 0 && offsets[n_entries] == n_rows;
   for (uint32_t e = 0; ok && e < n_entries; ++e)
      ok = offsets[e] <= offsets[e + 1];
   if (!ok) {
      warning("mszx index: corrupt entry offsets\n");
      return NULL;
   }

   mszx_index_t* idx = calloc(1, sizeof(*idx));
   if (!idx) return NULL;
   idx->base = p;
   idx->len = len;
   idx->n_rows = n_rows;
   idx->n_entries = n_entries;
   idx->offsets = offsets;

   const char* dir = p + IDX_HEADER_SIZE + entries_bytes;
   for (uint32_t i = 0; i < n_cols; ++i, dir += IDX_COLDIR_SIZE) {
      uint32_t id = get_u32(dir);
      uint32_t dt = get_u32(dir + 4);
      uint64_t off = get_u64(dir + 8);
      uint64_t bytes = get_u64(dir + 16);
      size_t es = dtype_size((int)dt);
      if (id >= MAX_COLUMN_ID || is_derived((int)id) || es == 0)
         continue; /* unknown: skip */
      if (bytes != n_rows * es || !range_ok(len, off, bytes) || off % 8) {
         warning("mszx index: corrupt column %u\n", id);
         free(idx);
         return NULL;
      }
      idx->cols[id].data = p + off;
      idx->cols[id].dtype = (int)dt;
   }
   for (uint32_t k = 0; k < n_keys; ++k, dir += IDX_KEYDIR_SIZE) {
      uint32_t id = get_u32(dir);
      uint32_t kind = get_u32(dir + 4);
      uint64_t n_valid = get_u64(dir + 8);
      uint64_t perm_off = get_u64(dir + 16);
      /* A key reads values from its column, so it needs one. */
      if (id >= MAX_COLUMN_ID || !idx->cols[id].data) continue;
      if (kind == MSZX_KEY_RUNS) {
         idx->keys[id].kind = MSZX_KEY_RUNS;
         idx->keys[id].n_valid = n_rows;
         continue;
      }
      if (kind != MSZX_KEY_PERM) continue;
      if (n_valid > n_rows || !range_ok(len, perm_off, n_valid * 4) ||
          perm_off % 8) {
         warning("mszx index: corrupt sorted key %u\n", id);
         free(idx);
         return NULL;
      }
      const uint32_t* perm = (const uint32_t*)(p + perm_off);
      for (uint64_t i = 0; i < n_valid; ++i) {
         if (perm[i] >= n_rows) {
            warning("mszx index: corrupt permutation for key %u\n", id);
            free(idx);
            return NULL;
         }
      }
      idx->keys[id].kind = MSZX_KEY_PERM;
      idx->keys[id].perm = perm;
      idx->keys[id].n_valid = n_valid;
   }
   return idx;
}

void mszx_index_close(mszx_index_t* idx) { free(idx); }

uint64_t mszx_index_n_rows(const mszx_index_t* idx) {
   return idx ? idx->n_rows : 0;
}

uint32_t mszx_index_n_entries(const mszx_index_t* idx) {
   return idx ? idx->n_entries : 0;
}

int mszx_index_has_column(const mszx_index_t* idx, int column) {
   return idx && column > 0 && column < MAX_COLUMN_ID &&
          (is_derived(column) || idx->cols[column].data != NULL);
}

static double load_value(const char* data, int dtype, uint64_t i) {
   switch (dtype) {
      case MSZX_DT_U8: return (double)((const uint8_t*)data)[i];
      case MSZX_DT_I8: return (double)((const int8_t*)data)[i];
      case MSZX_DT_U16: return (double)((const uint16_t*)data)[i];
      case MSZX_DT_I16: return (double)((const int16_t*)data)[i];
      case MSZX_DT_U32: return (double)((const uint32_t*)data)[i];
      case MSZX_DT_I32: return (double)((const int32_t*)data)[i];
      case MSZX_DT_U64: return (double)((const uint64_t*)data)[i];
      case MSZX_DT_I64: return (double)((const int64_t*)data)[i];
      case MSZX_DT_F32: return (double)((const float*)data)[i];
      case MSZX_DT_F64: return ((const double*)data)[i];
      default: return NAN;
   }
}

/* The entry owning `row`: the last e with offsets[e] <= row. */
static uint32_t row_entry(const mszx_index_t* idx, uint64_t row) {
   uint32_t a = 0, b = idx->n_entries;
   while (b - a > 1) {
      uint32_t m = a + (b - a) / 2;
      if (idx->offsets[m] <= row) a = m; else b = m;
   }
   return a;
}

double mszx_index_value(const mszx_index_t* idx, int column, uint64_t row) {
   if (!mszx_index_has_column(idx, column) || row >= idx->n_rows) return NAN;
   if (column == MSZX_COL_ENTRY) return (double)row_entry(idx, row);
   if (column == MSZX_COL_SPECTRUM_IDX)
      return (double)(row - idx->offsets[row_entry(idx, row)]);
   return load_value(idx->cols[column].data, idx->cols[column].dtype, row);
}

/* [*first, *last) are the rows in [a, b) whose value lies in [lo, hi], for a
 * column that never decreases over [a, b). */
static void run_bounds(const col_view_t* c, uint64_t a, uint64_t b, double lo,
                       double hi, uint64_t* first, uint64_t* last) {
   uint64_t end = b;
   while (a < b) { /* first value >= lo */
      uint64_t m = a + (b - a) / 2;
      if (load_value(c->data, c->dtype, m) < lo) a = m + 1; else b = m;
   }
   *first = a;
   b = end;
   while (a < b) { /* first value > hi */
      uint64_t m = a + (b - a) / 2;
      if (load_value(c->data, c->dtype, m) <= hi) a = m + 1; else b = m;
   }
   *last = a;
}

/* [*first, *last) are the perm positions whose value lies in [lo, hi]. */
static void perm_bounds(const col_view_t* c, const key_view_t* k, double lo,
                        double hi, uint64_t* first, uint64_t* last) {
   uint64_t a = 0, b = k->n_valid;
   while (a < b) {
      uint64_t m = a + (b - a) / 2;
      if (load_value(c->data, c->dtype, k->perm[m]) < lo) a = m + 1; else b = m;
   }
   *first = a;
   b = k->n_valid;
   while (a < b) {
      uint64_t m = a + (b - a) / 2;
      if (load_value(c->data, c->dtype, k->perm[m]) <= hi) a = m + 1; else b = m;
   }
   *last = a;
}

static int cmp_u32(const void* a, const void* b) {
   uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
   return x < y ? -1 : (x > y);
}

static int term_matches(const mszx_index_t* idx, const mszx_index_term_t* t,
                        uint64_t row) {
   double v = mszx_index_value(idx, t->column, row);
   return v >= t->lo && v <= t->hi; /* NaN never matches */
}

/* Whether `row` matches every term but `skip` (the one that produced it). */
static int row_matches(const mszx_index_t* idx, const mszx_index_term_t* terms,
                       size_t n_terms, long long skip, uint64_t row) {
   for (size_t t = 0; t < n_terms; ++t)
      if ((long long)t != skip && !term_matches(idx, &terms[t], row)) return 0;
   return 1;
}

/* Candidate rows for one term: either a perm slice or one row range per
 * entry (rows already ascending). */
typedef struct {
   long long term; /* -1: no term, every row */
   uint64_t count;
   const uint32_t* perm; /* perm slice [first, last) when set */
   uint64_t first, last;
   uint64_t* run_first; /* else per-entry [run_first[e], run_last[e]) */
   uint64_t* run_last;
} candidate_t;

/* Fill `c` for term `t` if it has a fast path; 0 if not, -1 on OOM. */
static int term_candidate(const mszx_index_t* idx, const mszx_index_term_t* t,
                          long long term, candidate_t* c) {
   int col = t->column;
   memset(c, 0, sizeof(*c));
   c->term = term;
   if (col != MSZX_COL_ENTRY && idx->keys[col].kind == MSZX_KEY_PERM) {
      perm_bounds(&idx->cols[col], &idx->keys[col], t->lo, t->hi, &c->first,
                  &c->last);
      c->perm = idx->keys[col].perm;
      c->count = c->last - c->first;
      return 1;
   }
   if (col != MSZX_COL_ENTRY && idx->keys[col].kind != MSZX_KEY_RUNS) return 0;

   size_t ne = idx->n_entries ? idx->n_entries : 1;
   c->run_first = malloc(ne * sizeof(uint64_t));
   c->run_last = malloc(ne * sizeof(uint64_t));
   if (!c->run_first || !c->run_last) {
      free(c->run_first);
      free(c->run_last);
      return -1;
   }
   for (uint32_t e = 0; e < idx->n_entries; ++e) {
      uint64_t a = idx->offsets[e], b = idx->offsets[e + 1];
      if (col == MSZX_COL_ENTRY) {
         if (!(e >= t->lo && e <= t->hi)) a = b;
         c->run_first[e] = a;
         c->run_last[e] = b;
      } else {
         run_bounds(&idx->cols[col], a, b, t->lo, t->hi, &c->run_first[e],
                    &c->run_last[e]);
      }
      c->count += c->run_last[e] - c->run_first[e];
   }
   return 1;
}

static void free_candidate(candidate_t* c) {
   free(c->run_first);
   free(c->run_last);
}

int mszx_index_query(const mszx_index_t* idx, const mszx_index_term_t* terms,
                     size_t n_terms, uint32_t** out_rows, size_t* out_n) {
   if (!idx || !out_rows || !out_n || (n_terms && !terms)) return -1;
   for (size_t t = 0; t < n_terms; ++t) {
      if (!mszx_index_has_column(idx, terms[t].column)) {
         warning("mszx index: no column %d in this index\n", terms[t].column);
         return -1;
      }
   }

   /* Drive the scan from the most selective term with a fast path. */
   candidate_t best = {-1, idx->n_rows, NULL, 0, idx->n_rows, NULL, NULL};
   for (size_t t = 0; t < n_terms; ++t) {
      candidate_t c;
      int rc = term_candidate(idx, &terms[t], (long long)t, &c);
      if (rc < 0) {
         free_candidate(&best);
         return -1;
      }
      if (rc == 0) continue;
      if (best.term < 0 || c.count < best.count) {
         free_candidate(&best);
         best = c;
      } else {
         free_candidate(&c);
      }
   }

   uint32_t* rows = malloc((best.count ? best.count : 1) * sizeof(uint32_t));
   if (!rows) {
      free_candidate(&best);
      return -1;
   }
   size_t n = 0;

   if (best.perm) {
      for (uint64_t i = best.first; i < best.last; ++i)
         if (row_matches(idx, terms, n_terms, best.term, best.perm[i]))
            rows[n++] = best.perm[i];
      /* Report hits in row order, i.e. grouped by entry. */
      if (n > 1) qsort(rows, n, sizeof(uint32_t), cmp_u32);
   } else if (best.run_first) {
      for (uint32_t e = 0; e < idx->n_entries; ++e)
         for (uint64_t r = best.run_first[e]; r < best.run_last[e]; ++r)
            if (row_matches(idx, terms, n_terms, best.term, r))
               rows[n++] = (uint32_t)r;
   } else {
      for (uint64_t r = 0; r < idx->n_rows; ++r)
         if (row_matches(idx, terms, n_terms, best.term, r))
            rows[n++] = (uint32_t)r;
   }

   free_candidate(&best);
   *out_rows = rows;
   *out_n = n;
   return 0;
}

int mszx_index_range(const mszx_index_t* idx, int column, double lo, double hi,
                     uint32_t** out_rows, size_t* out_n) {
   mszx_index_term_t t = {column, lo, hi};
   return mszx_index_query(idx, &t, 1, out_rows, out_n);
}

/* Parse "rt=600:660,ms_level=2,precursor_mz=500:" into terms. A bare value is
 * an equality test; either side of ':' may be left open. */
int mszx_index_parse_query(const char* text, mszx_index_term_t** out_terms,
                           size_t* out_n) {
   if (!text || !out_terms || !out_n) return -1;
   char* copy = strdup(text);
   if (!copy) return -1;

   size_t cap = 4, n = 0;
   mszx_index_term_t* terms = malloc(cap * sizeof(*terms));
   if (!terms) {
      free(copy);
      return -1;
   }

   int rc = 0;
   char* save = copy;
   for (char* tok = save; tok && *tok && rc == 0;) {
      char* next = strchr(tok, ',');
      if (next) *next++ = '\0';
      while (*tok == ' ') tok++;
      if (*tok == '\0') { tok = next; continue; }

      char* eq = strchr(tok, '=');
      if (!eq) {
         error("--query: expected key=value, got '%s'\n", tok);
         rc = -1;
         break;
      }
      *eq = '\0';
      char* key = tok;
      char* val = eq + 1;
      size_t kl = strlen(key);
      while (kl > 0 && key[kl - 1] == ' ') key[--kl] = '\0';

      int col = mszx_index_column_id(key);
      if (col < 0) {
         error("--query: unknown column '%s'\n", key);
         rc = -1;
         break;
      }

      double lo = -INFINITY, hi = INFINITY;
      char* colon = strchr(val, ':');
      char* e;
      if (colon) {
         *colon = '\0';
         if (*val) {
            lo = strtod(val, &e);
            if (e == val || *e) rc = -1;
         }
         if (colon[1]) {
            hi = strtod(colon + 1, &e);
            if (e == colon + 1 || *e) rc = -1;
         }
      } else {
         lo = hi = strtod(val, &e);
         if (e == val || *e) rc = -1;
      }
      if (rc != 0) {
         error("--query: bad value for '%s'\n", key);
         break;
      }

      if (n == cap) {
         cap *= 2;
         mszx_index_term_t* tmp = realloc(terms, cap * sizeof(*tmp));
         if (!tmp) { rc = -1; break; }
         terms = tmp;
      }
      terms[n].column = col;
      terms[n].lo = lo;
      terms[n].hi = hi;
      n++;
      tok = next;
   }

   free(copy);
   if (rc != 0) {
      free(terms);
      return -1;
   }
   *out_terms = terms;
   *out_n = n;
   return 0;
}
