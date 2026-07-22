/**
 * @file mszx_dict.c
 * @brief Investigation/prototype tool: ZSTD-dictionary compression of many
 *        small mzML packed into one .mszx archive, plus a dict-size sweep.
 *
 * Branch feat/mszx-zstd-dictionary. Links the mscompress core so it can reuse
 * the real .msz stream-stripping logic (parse_footer + per-block ZSTD frames)
 * to recover the raw XML / m/z / intensity streams exactly as the format
 * stores them, then measures three compression schemes on those streams:
 *
 *   (i)   per-file independent   (current behavior; the thing to beat)
 *   (ii)  single pooled stream   (no dict; the prior micro-experiment winner)
 *   (iii) shared ZSTD dictionary (ZDICT fastCover, stored once)
 *
 * Composable primitives (a bash driver orchestrates + parallelizes):
 *   dump    <in.msz> <out_xml> <out_mz> <out_inten>
 *   train   <maxdict> <chunk_kb> <budget_mb> <level> <dictout> <sample...>
 *   cfile   <level> <dict|-> <infile>                (one single-thread frame)
 *   pool    <level> <ldm 0|1> <infile...>           (one single-thread frame)
 *   archive <out.mszx> <level> <mode> <maxdict> <manifest.tsv>
 *
 * Every number printed comes from actually running zstd/ZDICT on real bytes.
 */

#define ZDICT_STATIC_LINKING_ONLY
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zstd.h"
#include "zdict.h"

#include "mscompress.h" /* mscompress core (reused for .msz extraction) */

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static double now_ms(void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

typedef struct {
   uint8_t* data;
   size_t len;
} buf_t;

static int read_whole_file(const char* path, buf_t* out) {
   FILE* f = fopen(path, "rb");
   if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
   fseek(f, 0, SEEK_END);
   long sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   if (sz < 0) { fclose(f); return -1; }
   uint8_t* b = malloc((size_t)sz ? (size_t)sz : 1);
   if (!b) { fclose(f); return -1; }
   size_t got = fread(b, 1, (size_t)sz, f);
   fclose(f);
   if (got != (size_t)sz) { free(b); return -1; }
   out->data = b;
   out->len = (size_t)sz;
   return 0;
}

static int write_whole_file(const char* path, const void* data, size_t len) {
   FILE* f = fopen(path, "wb");
   if (!f) { fprintf(stderr, "cannot create %s\n", path); return -1; }
   size_t w = fwrite(data, 1, len, f);
   fclose(f);
   return w == len ? 0 : -1;
}

/* Plain single-thread zstd frame. Optional long-distance matching + explicit
 * windowLog (0 = zstd default). LDM with a large window is what lets a pooled
 * frame match across multi-MB files; without it zstd's ~8 MB default window
 * can't span them. */
static uint8_t* zc(ZSTD_CCtx* cctx, const void* src, size_t srclen, int level,
                   int ldm, int window_log, size_t* outlen) {
   ZSTD_CCtx_reset(cctx, ZSTD_reset_session_and_parameters);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   if (ldm) ZSTD_CCtx_setParameter(cctx, ZSTD_c_enableLongDistanceMatching, 1);
   if (window_log > 0)
      ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, window_log);
   size_t bound = ZSTD_compressBound(srclen);
   uint8_t* dst = malloc(bound ? bound : 1);
   if (!dst) return NULL;
   size_t r = ZSTD_compress2(cctx, dst, bound, src, srclen);
   if (ZSTD_isError(r)) { fprintf(stderr, "zc: %s\n", ZSTD_getErrorName(r)); free(dst); return NULL; }
   *outlen = r;
   return dst;
}

/* Dictionary single-thread zstd frame (raw-content dict via loadDictionary). */
static uint8_t* zc_dict(ZSTD_CCtx* cctx, const void* src, size_t srclen,
                        const void* dict, size_t dictlen, int level,
                        size_t* outlen) {
   ZSTD_CCtx_reset(cctx, ZSTD_reset_session_and_parameters);
   ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
   if (ZSTD_isError(ZSTD_CCtx_loadDictionary(cctx, dict, dictlen))) return NULL;
   size_t bound = ZSTD_compressBound(srclen);
   uint8_t* dst = malloc(bound ? bound : 1);
   if (!dst) return NULL;
   size_t r = ZSTD_compress2(cctx, dst, bound, src, srclen);
   if (ZSTD_isError(r)) { free(dst); return NULL; }
   *outlen = r;
   return dst;
}

/* Train a dictionary of up to maxdict bytes.
 *
 * Chunks each sample stream into chunk_bytes pieces and stride-subsamples them
 * down to budget_bytes so ZDICT sees many representative small samples (the
 * standard dictionary-training regime) and trains quickly. Uses fastCover with
 * fixed parameters (no slow COVER optimization). Returns malloc'd dict. */
static uint8_t* train_dict(buf_t* streams, size_t nstreams, size_t maxdict,
                           size_t chunk_bytes, size_t budget_bytes, int level,
                           size_t* out_dictlen, double* out_train_ms) {
   /* enumerate all chunks */
   size_t total_chunks = 0;
   for (size_t i = 0; i < nstreams; i++)
      total_chunks += (streams[i].len + chunk_bytes - 1) / chunk_bytes;
   if (total_chunks == 0) return NULL;

   size_t budget_chunks = budget_bytes / chunk_bytes;
   if (budget_chunks == 0) budget_chunks = 1;
   size_t stride = total_chunks > budget_chunks
                       ? (total_chunks + budget_chunks - 1) / budget_chunks
                       : 1;

   /* first pass: count kept chunks + total bytes */
   size_t kept = 0, keep_bytes = 0, gi = 0;
   for (size_t i = 0; i < nstreams; i++) {
      for (size_t off = 0; off < streams[i].len; off += chunk_bytes, gi++) {
         if (gi % stride) continue;
         size_t clen = streams[i].len - off;
         if (clen > chunk_bytes) clen = chunk_bytes;
         kept++;
         keep_bytes += clen;
      }
   }
   uint8_t* concat = malloc(keep_bytes ? keep_bytes : 1);
   size_t* sizes = malloc(kept * sizeof(size_t));
   if (!concat || !sizes) { free(concat); free(sizes); return NULL; }
   size_t co = 0, si = 0;
   gi = 0;
   for (size_t i = 0; i < nstreams; i++) {
      for (size_t off = 0; off < streams[i].len; off += chunk_bytes, gi++) {
         if (gi % stride) continue;
         size_t clen = streams[i].len - off;
         if (clen > chunk_bytes) clen = chunk_bytes;
         memcpy(concat + co, streams[i].data + off, clen);
         co += clen;
         sizes[si++] = clen;
      }
   }

   uint8_t* dict = malloc(maxdict);
   if (!dict) { free(concat); free(sizes); return NULL; }

   ZDICT_fastCover_params_t p;
   memset(&p, 0, sizeof(p));
   p.d = 8;
   p.k = 1024;
   p.steps = 0; /* fixed params, no optimization */
   p.zParams.compressionLevel = level;

   double t0 = now_ms();
   size_t dlen = ZDICT_trainFromBuffer_fastCover(dict, maxdict, concat, sizes,
                                                 (unsigned)si, p);
   double t1 = now_ms();
   free(concat);
   free(sizes);
   if (ZDICT_isError(dlen)) {
      fprintf(stderr, "ZDICT fastCover(maxdict=%zu, samples=%zu) failed: %s\n",
              maxdict, si, ZDICT_getErrorName(dlen));
      free(dict);
      return NULL;
   }
   *out_dictlen = dlen;
   *out_train_ms = t1 - t0;
   return dict;
}

/* ------------------------------------------------------------------ */
/* dump: recover raw XML / m/z / intensity streams from a .msz         */
/* ------------------------------------------------------------------ */

static uint8_t* extract_stream(char* input_map, uint64_t base_pos,
                               block_len_queue_t* q, ZSTD_DCtx* dctx,
                               size_t* out_len) {
   size_t cap = 0, len = 0;
   uint8_t* out = NULL;
   uint64_t off = 0;
   block_len_t* blk;
   while ((blk = pop_block_len(q)) != NULL) {
      if (blk->compressed_size == 0 || blk->original_size == 0) continue;
      const void* src = (const uint8_t*)input_map + base_pos + off;
      if (len + blk->original_size > cap) {
         cap = (len + blk->original_size) * 2;
         uint8_t* tmp = realloc(out, cap);
         if (!tmp) { free(out); return NULL; }
         out = tmp;
      }
      size_t r = ZSTD_decompressDCtx(dctx, out + len, blk->original_size, src,
                                     blk->compressed_size);
      if (ZSTD_isError(r) || r != blk->original_size) {
         fprintf(stderr, "block decompress failed: %s\n", ZSTD_getErrorName(r));
         free(out);
         return NULL;
      }
      len += r;
      off += blk->compressed_size;
   }
   *out_len = len;
   return out ? out : (uint8_t*)calloc(1, 1);
}

static int cmd_dump(int argc, char** argv) {
   if (argc < 6) {
      fprintf(stderr,
              "usage: mszx_dict dump <in.msz> <out_xml> <out_mz> <out_inten>\n");
      return 2;
   }
   const char* in = argv[2];
   int fd = open_input_file((char*)in);
   if (fd < 0) { fprintf(stderr, "cannot open %s\n", in); return 1; }
   size_t fsize = get_filesize((char*)in);
   char* map = get_mapping(fd);
   if (!map) { fprintf(stderr, "mmap failed\n"); close_file(fd); return 1; }

   footer_t* footer;
   block_len_queue_t *xmlq, *mzq, *intq;
   divisions_t* divs;
   int n_div = 0;
   parse_footer(&footer, map, (long)fsize, &xmlq, &mzq, &intq, &divs, &n_div);
   if (n_div == 0) { fprintf(stderr, "no divisions in %s\n", in); return 1; }

   ZSTD_DCtx* dctx = ZSTD_createDCtx();
   size_t xlen = 0, mlen = 0, ilen = 0;
   uint8_t* xml = extract_stream(map, footer->xml_pos, xmlq, dctx, &xlen);
   uint8_t* mz = extract_stream(map, footer->mz_binary_pos, mzq, dctx, &mlen);
   uint8_t* inten =
       extract_stream(map, footer->inten_binary_pos, intq, dctx, &ilen);
   ZSTD_freeDCtx(dctx);

   int rc = 0;
   if (!xml || !mz || !inten) rc = 1;
   if (!rc && write_whole_file(argv[3], xml, xlen)) rc = 1;
   if (!rc && write_whole_file(argv[4], mz, mlen)) rc = 1;
   if (!rc && write_whole_file(argv[5], inten, ilen)) rc = 1;
   if (!rc) printf("dump %s xml=%zu mz=%zu inten=%zu\n", in, xlen, mlen, ilen);
   free(xml); free(mz); free(inten);
   close_file(fd);
   return rc;
}

/* ------------------------------------------------------------------ */
/* train: train one dict over sample streams, write to file            */
/* ------------------------------------------------------------------ */

static int cmd_train(int argc, char** argv) {
   if (argc < 7) {
      fprintf(stderr, "usage: mszx_dict train <maxdict> <chunk_kb> <budget_mb> "
                      "<level> <dictout> <sample...>\n");
      return 2;
   }
   size_t maxdict = (size_t)strtoull(argv[2], NULL, 10);
   size_t chunk = (size_t)strtoull(argv[3], NULL, 10) * 1024;
   size_t budget = (size_t)strtoull(argv[4], NULL, 10) * 1024 * 1024;
   int level = atoi(argv[5]);
   const char* dictout = argv[6];
   int n = argc - 7;
   char** paths = argv + 7;

   buf_t* s = calloc(n, sizeof(buf_t));
   for (int i = 0; i < n; i++)
      if (read_whole_file(paths[i], &s[i])) return 1;

   size_t dlen = 0;
   double ms = 0;
   uint8_t* dict = train_dict(s, n, maxdict, chunk, budget, level, &dlen, &ms);
   if (!dict) return 1;
   if (write_whole_file(dictout, dict, dlen)) return 1;
   printf("train maxdict=%zu actual=%zu samples_from=%d chunk_kb=%zu "
          "budget_mb=%zu train_ms=%.1f\n",
          maxdict, dlen, n, chunk / 1024, budget / (1024 * 1024), ms);
   free(dict);
   for (int i = 0; i < n; i++) free(s[i].data);
   free(s);
   return 0;
}

/* ------------------------------------------------------------------ */
/* cfile: compress one file (optional dict), print size + ms           */
/* ------------------------------------------------------------------ */

static int cmd_cfile(int argc, char** argv) {
   if (argc < 5) {
      fprintf(stderr, "usage: mszx_dict cfile <level> <dict|-> <infile>\n");
      return 2;
   }
   int level = atoi(argv[2]);
   const char* dictpath = argv[3];
   const char* infile = argv[4];

   buf_t in;
   if (read_whole_file(infile, &in)) return 1;
   buf_t dict = {NULL, 0};
   int use_dict = strcmp(dictpath, "-") != 0;
   if (use_dict && read_whole_file(dictpath, &dict)) return 1;

   ZSTD_CCtx* cctx = ZSTD_createCCtx();
   size_t clen = 0;
   double t0 = now_ms();
   uint8_t* c = use_dict
                    ? zc_dict(cctx, in.data, in.len, dict.data, dict.len, level, &clen)
                    : zc(cctx, in.data, in.len, level, 0, 0, &clen);
   double ms = now_ms() - t0;
   ZSTD_freeCCtx(cctx);
   if (!c) { fprintf(stderr, "cfile: compress failed\n"); return 1; }
   /* machine-readable: SIZE<tab>raw<tab>ms<tab>path */
   printf("%zu\t%zu\t%.1f\t%s\n", clen, in.len, ms, infile);
   free(c);
   free(in.data);
   free(dict.data);
   return 0;
}

/* ------------------------------------------------------------------ */
/* pool: concatenate + single-thread compress, print size + ms         */
/* ------------------------------------------------------------------ */

static int cmd_pool(int argc, char** argv) {
   if (argc < 6) {
      fprintf(stderr,
              "usage: mszx_dict pool <level> <ldm 0|1> <windowlog|0> <infile...>\n");
      return 2;
   }
   int level = atoi(argv[2]);
   int ldm = atoi(argv[3]);
   int window_log = atoi(argv[4]);
   int n = argc - 5;
   char** paths = argv + 5;

   size_t total = 0;
   buf_t* files = calloc(n, sizeof(buf_t));
   for (int i = 0; i < n; i++) {
      if (read_whole_file(paths[i], &files[i])) return 1;
      total += files[i].len;
   }
   uint8_t* pool = malloc(total ? total : 1);
   size_t off = 0;
   for (int i = 0; i < n; i++) {
      memcpy(pool + off, files[i].data, files[i].len);
      off += files[i].len;
      free(files[i].data);
   }
   free(files);

   ZSTD_CCtx* cctx = ZSTD_createCCtx();
   size_t clen = 0;
   double t0 = now_ms();
   uint8_t* c = zc(cctx, pool, total, level, ldm, window_log, &clen);
   double ms = now_ms() - t0;
   ZSTD_freeCCtx(cctx);
   free(pool);
   if (!c) return 1;
   printf("%zu\t%zu\t%.1f\t%d\n", clen, total, ms, n); /* size raw ms nfiles */
   free(c);
   return 0;
}

/* ------------------------------------------------------------------ */
/* archive: build a real .mszx + round-trip verify (dict modes)        */
/* ------------------------------------------------------------------ */

#define TAR_BLK 512

static void octal_field(char* dst, size_t len, uint64_t val) {
   char tmp[32];
   snprintf(tmp, sizeof(tmp), "%0*llo", (int)len - 1, (unsigned long long)val);
   memcpy(dst, tmp, len - 1);
   dst[len - 1] = '\0';
}

static int tar_write_entry(FILE* f, const char* name, const void* data,
                           size_t len) {
   char hdr[TAR_BLK];
   memset(hdr, 0, sizeof(hdr));
   size_t nlen = strlen(name);
   if (nlen > 100) { fprintf(stderr, "name too long: %s\n", name); return -1; }
   memcpy(hdr + 0, name, nlen);
   octal_field(hdr + 100, 8, 0000644);
   octal_field(hdr + 108, 8, 0);
   octal_field(hdr + 116, 8, 0);
   octal_field(hdr + 124, 12, len);
   octal_field(hdr + 136, 12, 0);
   memset(hdr + 148, ' ', 8);
   hdr[156] = '0';
   memcpy(hdr + 257, "ustar", 5);
   hdr[257 + 5] = '\0';
   hdr[263] = '0'; hdr[264] = '0';
   unsigned sum = 0;
   for (int i = 0; i < TAR_BLK; i++) sum += (unsigned char)hdr[i];
   char cksum[8];
   snprintf(cksum, sizeof(cksum), "%06o", sum);
   memcpy(hdr + 148, cksum, 6);
   hdr[148 + 6] = '\0';
   hdr[148 + 7] = ' ';
   if (fwrite(hdr, 1, TAR_BLK, f) != TAR_BLK) return -1;
   if (len && fwrite(data, 1, len, f) != len) return -1;
   size_t pad = (TAR_BLK - (len % TAR_BLK)) % TAR_BLK;
   if (pad) {
      char z[TAR_BLK];
      memset(z, 0, sizeof(z));
      if (fwrite(z, 1, pad, f) != pad) return -1;
   }
   return 0;
}

static int tar_finish(FILE* f) {
   char z[TAR_BLK * 2];
   memset(z, 0, sizeof(z));
   return fwrite(z, 1, sizeof(z), f) == sizeof(z) ? 0 : -1;
}

static int read_manifest(const char* path, char*** rows, int* nrows) {
   FILE* f = fopen(path, "r");
   if (!f) { fprintf(stderr, "cannot open manifest %s\n", path); return -1; }
   char** out = NULL;
   int n = 0, cap = 0;
   char line[8192];
   while (fgets(line, sizeof(line), f)) {
      size_t l = strlen(line);
      while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
      if (l == 0) continue;
      if (n == cap) { cap = cap ? cap * 2 : 16; out = realloc(out, cap * sizeof(char*)); }
      out[n++] = strdup(line);
   }
   fclose(f);
   *rows = out;
   *nrows = n;
   return 0;
}

static int split_tsv(char* line, char* out[3]) {
   int n = 0;
   char* p = line;
   out[n++] = p;
   while (*p && n < 3) {
      if (*p == '\t') { *p = 0; out[n++] = p + 1; }
      p++;
   }
   return n;
}

enum { M_PERFILE, M_POOL, M_DICTXML, M_DICTALL };

static int cmd_archive(int argc, char** argv) {
   if (argc < 7) {
      fprintf(stderr, "usage: mszx_dict archive <out.mszx> <level> "
                      "<perfile|pool|dictxml|dictall> <maxdict> <manifest.tsv>\n");
      return 2;
   }
   const char* out = argv[2];
   int level = atoi(argv[3]);
   const char* modestr = argv[4];
   size_t maxdict = (size_t)strtoull(argv[5], NULL, 10);
   const char* manifest = argv[6];

   int mode;
   if (!strcmp(modestr, "perfile")) mode = M_PERFILE;
   else if (!strcmp(modestr, "pool")) mode = M_POOL;
   else if (!strcmp(modestr, "dictxml")) mode = M_DICTXML;
   else if (!strcmp(modestr, "dictall")) mode = M_DICTALL;
   else { fprintf(stderr, "bad mode %s\n", modestr); return 2; }

   char** rows;
   int nfiles;
   if (read_manifest(manifest, &rows, &nfiles)) return 1;
   if (nfiles == 0) { fprintf(stderr, "empty manifest\n"); return 1; }

   buf_t* xml = calloc(nfiles, sizeof(buf_t));
   buf_t* mz = calloc(nfiles, sizeof(buf_t));
   buf_t* inten = calloc(nfiles, sizeof(buf_t));
   for (int i = 0; i < nfiles; i++) {
      char* cols[3];
      if (split_tsv(rows[i], cols) < 3) { fprintf(stderr, "manifest line %d needs 3 cols\n", i); return 1; }
      if (read_whole_file(cols[0], &xml[i])) return 1;
      if (read_whole_file(cols[1], &mz[i])) return 1;
      if (read_whole_file(cols[2], &inten[i])) return 1;
   }

   FILE* f = fopen(out, "wb");
   if (!f) { fprintf(stderr, "cannot create %s\n", out); return 1; }
   ZSTD_CCtx* cctx = ZSTD_createCCtx();

   const size_t CHUNK = 16 * 1024, BUDGET = 64 * 1024 * 1024;
   uint8_t *xdict = NULL, *mdict = NULL, *idict = NULL;
   size_t xdlen = 0, mdlen = 0, idlen = 0;
   double tms = 0;
   if (mode == M_DICTXML || mode == M_DICTALL) {
      xdict = train_dict(xml, nfiles, maxdict, CHUNK, BUDGET, level, &xdlen, &tms);
      if (!xdict) return 1;
      tar_write_entry(f, "dictionary_xml.zstd", xdict, xdlen);
   }
   if (mode == M_DICTALL) {
      mdict = train_dict(mz, nfiles, maxdict, CHUNK, BUDGET, level, &mdlen, &tms);
      idict = train_dict(inten, nfiles, maxdict, CHUNK, BUDGET, level, &idlen, &tms);
      if (!mdict || !idict) return 1;
      tar_write_entry(f, "dictionary_mz.zstd", mdict, mdlen);
      tar_write_entry(f, "dictionary_inten.zstd", idict, idlen);
   }

   char mj[256];
   int mjl = snprintf(mj, sizeof(mj),
                      "{\"mode\":\"%s\",\"level\":%d,\"maxdict\":%zu,"
                      "\"nfiles\":%d,\"dict_xml\":%zu}\n",
                      modestr, level, maxdict, nfiles, xdlen);
   tar_write_entry(f, "manifest.json", mj, (size_t)mjl);

   char name[64];
   int rc = 0;
   if (mode == M_POOL) {
      buf_t* groups[3] = {xml, mz, inten};
      const char* gn[3] = {"pool.xml.zst", "pool.mz.zst", "pool.inten.zst"};
      for (int g = 0; g < 3 && !rc; g++) {
         size_t tot = 0;
         for (int i = 0; i < nfiles; i++) tot += groups[g][i].len;
         uint8_t* pool = malloc(tot ? tot : 1);
         size_t off = 0;
         for (int i = 0; i < nfiles; i++) { memcpy(pool + off, groups[g][i].data, groups[g][i].len); off += groups[g][i].len; }
         size_t clen = 0;
         uint8_t* c = zc(cctx, pool, tot, level, 0, 0, &clen);
         free(pool);
         if (!c) { rc = 1; break; }
         tar_write_entry(f, gn[g], c, clen);
         free(c);
      }
   } else {
      for (int i = 0; i < nfiles && !rc; i++) {
         struct { buf_t* g; uint8_t* d; size_t dl; const char* ext; } S[3] = {
             {xml, xdict, xdlen, "xml"},
             {mz, mode == M_DICTALL ? mdict : NULL, mode == M_DICTALL ? mdlen : 0, "mz"},
             {inten, mode == M_DICTALL ? idict : NULL, mode == M_DICTALL ? idlen : 0, "inten"},
         };
         if (mode == M_PERFILE) { S[0].d = NULL; S[0].dl = 0; }
         for (int s = 0; s < 3; s++) {
            size_t clen = 0;
            uint8_t* c = S[s].d
                             ? zc_dict(cctx, S[s].g[i].data, S[s].g[i].len, S[s].d, S[s].dl, level, &clen)
                             : zc(cctx, S[s].g[i].data, S[s].g[i].len, level, 0, 0, &clen);
            if (!c) { rc = 1; break; }
            snprintf(name, sizeof(name), "f%06d.%s.zst", i, S[s].ext);
            tar_write_entry(f, name, c, clen);
            free(c);
         }
      }
   }
   if (!rc) rc = tar_finish(f);
   fclose(f);
   ZSTD_freeCCtx(cctx);
   if (rc) { fprintf(stderr, "archive build failed\n"); return 1; }

   size_t archive_bytes = get_filesize((char*)out);
   printf("archive %s mode=%s level=%d maxdict=%zu nfiles=%d bytes=%zu\n", out,
          modestr, level, maxdict, nfiles, archive_bytes);

   /* round-trip verify (dict modes) by re-reading through the repo tar reader */
   int verify_rc = 0;
   if (mode == M_DICTXML || mode == M_DICTALL) {
      int fd = open_input_file((char*)out);
      mszx_entry_t* entries = NULL;
      size_t nent = 0;
      if (fd < 0 || mszx_list_entries(fd, &entries, &nent) != 0) {
         fprintf(stderr, "verify: cannot re-read archive\n");
         if (fd >= 0) close_file(fd);
         verify_rc = 1;
      } else {
         char* map = get_mapping(fd);
         uint8_t *vx = NULL, *vm = NULL, *vi = NULL;
         size_t vxl = 0, vml = 0, vil = 0;
         for (size_t e = 0; e < nent; e++) {
            uint8_t* p = (uint8_t*)map + entries[e].offset;
            if (!strcmp(entries[e].name, "dictionary_xml.zstd")) { vx = p; vxl = entries[e].size; }
            else if (!strcmp(entries[e].name, "dictionary_mz.zstd")) { vm = p; vml = entries[e].size; }
            else if (!strcmp(entries[e].name, "dictionary_inten.zstd")) { vi = p; vil = entries[e].size; }
         }
         ZSTD_DCtx* dctx = ZSTD_createDCtx();
         int checked = 0, failed = 0;
         for (size_t e = 0; e < nent; e++) {
            int idx; char ext[16];
            if (sscanf(entries[e].name, "f%d.%15[^.].zst", &idx, ext) != 2) continue;
            buf_t* orig = NULL; uint8_t* d = NULL; size_t dl = 0;
            if (!strcmp(ext, "xml")) { orig = &xml[idx]; d = vx; dl = vxl; }
            else if (!strcmp(ext, "mz")) { orig = &mz[idx]; d = (mode == M_DICTALL) ? vm : NULL; dl = (mode == M_DICTALL) ? vml : 0; }
            else if (!strcmp(ext, "inten")) { orig = &inten[idx]; d = (mode == M_DICTALL) ? vi : NULL; dl = (mode == M_DICTALL) ? vil : 0; }
            else continue;
            uint8_t* cp = (uint8_t*)map + entries[e].offset;
            size_t clen = entries[e].size;
            uint8_t* dec = malloc(orig->len ? orig->len : 1);
            size_t r = d ? ZSTD_decompress_usingDict(dctx, dec, orig->len, cp, clen, d, dl)
                         : ZSTD_decompressDCtx(dctx, dec, orig->len, cp, clen);
            if (ZSTD_isError(r) || r != orig->len || memcmp(dec, orig->data, orig->len) != 0) {
               fprintf(stderr, "verify FAIL entry %s\n", entries[e].name);
               failed++;
            }
            checked++;
            free(dec);
         }
         ZSTD_freeDCtx(dctx);
         printf("verify %s: %d entries checked, %d failed -> %s\n", out, checked,
                failed, failed ? "FAIL" : "PASS");
         if (failed) verify_rc = 1;
         mszx_free_entries(entries, nent);
         close_file(fd);
      }
   }

   free(xdict); free(mdict); free(idict);
   for (int i = 0; i < nfiles; i++) { free(xml[i].data); free(mz[i].data); free(inten[i].data); free(rows[i]); }
   free(xml); free(mz); free(inten); free(rows);
   return verify_rc;
}

/* ------------------------------------------------------------------ */

int main(int argc, char** argv) {
   if (argc < 2) {
      fprintf(stderr,
              "usage: mszx_dict <dump|train|cfile|pool|archive> ...\n");
      return 2;
   }
   if (!strcmp(argv[1], "dump")) return cmd_dump(argc, argv);
   if (!strcmp(argv[1], "train")) return cmd_train(argc, argv);
   if (!strcmp(argv[1], "cfile")) return cmd_cfile(argc, argv);
   if (!strcmp(argv[1], "pool")) return cmd_pool(argc, argv);
   if (!strcmp(argv[1], "archive")) return cmd_archive(argc, argv);
   fprintf(stderr, "unknown subcommand %s\n", argv[1]);
   return 2;
}
