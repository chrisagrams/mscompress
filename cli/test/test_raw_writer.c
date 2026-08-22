/**
 * @file test_raw_writer.c
 * @brief Unit tests for the vendor raw -> mzML writer in cli/raw_input.c.
 *
 * Everything here runs without libraw2ms_capi: spectra are synthesized and
 * fed through the provider interface, and the generated document is checked
 * with mscompress's own detectors (pattern_detect/scan_mzml) plus a full
 * preprocess+compress pass, since that is the contract the writer must
 * satisfy.
 */

#include "../raw_input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libbase64.h"
#include "mscompress.h"
#include "yxml.h"

/* Defined in preprocess.c; not part of the public header. */
extern yxml_t* alloc_yxml(void);

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg)                                             \
   do {                                                              \
      checks++;                                                      \
      if (!(cond)) {                                                 \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); \
         failures++;                                                 \
      }                                                              \
   } while (0)

#define RUN_TEST(fn)                                              \
   do {                                                           \
      int before = failures;                                      \
      fn();                                                       \
      printf("%-58s %s\n", #fn, failures == before ? "PASS" : "FAIL"); \
   } while (0)

/* --- synthetic run ------------------------------------------------------- */

static double g_mz0[4] = {100.5, 101.5, 102.5, 103.5};
static float g_int0[4] = {1.5f, 2.5f, 3.5f, 4.5f};
static double g_mz1[2] = {150.25, 300.5};
static float g_int1[2] = {10.0f, 20.0f};

static raw_spec_t g_specs[3];

static const raw_spec_t* test_fetch(void* ctx, uint64_t i) {
   (void)ctx;
   return i < 3 ? &g_specs[i] : NULL;
}

static void build_run(raw_run_t* run) {
   memset(g_specs, 0, sizeof(g_specs));

   g_specs[0].index = 0;
   g_specs[0].ms_level = 1;
   g_specs[0].polarity = RAW_POLARITY_POSITIVE;
   g_specs[0].peak_mode = RAW_PEAKS_PROFILE;
   g_specs[0].rt_seconds = 0.5;
   g_specs[0].precursor_mz = NAN;
   g_specs[0].isolation_lower = NAN;
   g_specs[0].isolation_upper = NAN;
   g_specs[0].injection_time_ms = NAN;
   g_specs[0].drift_time_ms = NAN;
   g_specs[0].precursor_id = -1;
   g_specs[0].charge = -1;
   g_specs[0].n_peaks = 4;
   g_specs[0].mz = g_mz0;
   g_specs[0].intensity = g_int0;
   g_specs[0].native_id = "scan=1";

   g_specs[1].index = 1;
   g_specs[1].ms_level = 2;
   g_specs[1].polarity = RAW_POLARITY_NEGATIVE;
   g_specs[1].peak_mode = RAW_PEAKS_CENTROID;
   g_specs[1].rt_seconds = 1.5;
   g_specs[1].injection_time_ms = 5.0;
   g_specs[1].precursor_mz = 456.7891;
   g_specs[1].isolation_lower = 456.2891; /* absolute bounds -> 0.5 offsets */
   g_specs[1].isolation_upper = 457.2891;
   g_specs[1].drift_time_ms = NAN;
   g_specs[1].precursor_id = 3;
   g_specs[1].charge = 2;
   g_specs[1].n_peaks = 2;
   g_specs[1].mz = g_mz1;
   g_specs[1].intensity = g_int1;
   g_specs[1].native_id = "scan=2";

   /* Empty spectrum: zero peaks, unknown polarity/peak mode. */
   g_specs[2].index = 2;
   g_specs[2].ms_level = 1;
   g_specs[2].polarity = 0;
   g_specs[2].peak_mode = 0;
   g_specs[2].rt_seconds = 2.5;
   g_specs[2].precursor_mz = NAN;
   g_specs[2].isolation_lower = NAN;
   g_specs[2].isolation_upper = NAN;
   g_specs[2].injection_time_ms = NAN;
   g_specs[2].drift_time_ms = NAN;
   g_specs[2].precursor_id = -1;
   g_specs[2].charge = -1;
   g_specs[2].n_peaks = 0;
   g_specs[2].mz = NULL;
   g_specs[2].intensity = NULL;
   g_specs[2].native_id = "scan=3";

   run->provider.ctx = NULL;
   run->provider.fetch = test_fetch;
   run->provider.release = NULL;
   run->n_spectra = 3;
   run->source_path = "/data/QC & run.raw";
   run->run_id = "20260822_QC"; /* digit-leading: must be NCName-sanitized */
   run->instrument_model = "Test Instrument X";
   run->raw2ms_version = "0.1.0";
   run->vendor = RAW_VENDOR_THERMO;
}

/* Writes the synthetic run and bails the test early if that fails. */
static int write_doc(mzml_buf_t* doc) {
   raw_run_t run;
   build_run(&run);
   memset(doc, 0, sizeof(*doc));
   if (raw_write_mzml_mem(&run, doc) != 0) {
      fprintf(stderr, "FAIL: raw_write_mzml_mem returned error\n");
      failures++;
      return -1;
   }
   return 0;
}

/* --- tests --------------------------------------------------------------- */

static void test_pattern_detect_reads_generated_mzml(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   data_format_t* df = pattern_detect(doc.buf);
   CHECK(df != NULL, "pattern_detect must accept the generated document");
   if (!df) {
      free(doc.buf);
      return;
   }
   CHECK(df->source_total_spec == 3, "spectrumList count detected as 3");
   CHECK(df->source_mz_fmt == _64d_, "m/z detected as 64-bit float");
   CHECK(df->source_inten_fmt == _32f_, "intensity detected as 32-bit float");
   CHECK(df->source_compression == _no_comp_, "arrays detected uncompressed");
   dealloc_df(df);
   free(doc.buf);
}

static void test_scan_mzml_locates_spectra_and_binaries(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   data_format_t* df = pattern_detect(doc.buf);
   CHECK(df != NULL, "pattern_detect must succeed");
   if (!df) {
      free(doc.buf);
      return;
   }
   division_t* div = scan_mzml(doc.buf, df, (long)doc.len, SCANNUM | MSLEVEL | RETTIME);
   CHECK(div != NULL, "scan_mzml must scan the generated document");
   if (div) {
      CHECK(div->mz->total_spec == 3, "three m/z arrays found");
      CHECK(div->inten->total_spec == 3, "three intensity arrays found");
      /* 4 doubles = 32 bytes -> 44 base64 chars. */
      CHECK(div->mz->end_positions[0] - div->mz->start_positions[0] == 44,
            "m/z binary of spectrum 0 is 44 chars");
      CHECK(div->mz->end_positions[1] - div->mz->start_positions[1] == 24,
            "m/z binary of spectrum 1 is 24 chars");
      CHECK(div->scans[0] == 1 && div->scans[1] == 2 && div->scans[2] == 3,
            "scan numbers parsed from native ids");
      CHECK(div->ms_levels[0] == 1 && div->ms_levels[1] == 2 &&
                div->ms_levels[2] == 1,
            "ms levels parsed");
      CHECK(fabs(div->ret_times[0] - 0.5) < 0.001 &&
                fabs(div->ret_times[1] - 1.5) < 0.001 &&
                fabs(div->ret_times[2] - 2.5) < 0.001,
            "retention times parsed in seconds");
      /* XML spans: header before first m/z, tail after last intensity. */
      CHECK(div->xml->start_positions[0] == 0, "first XML span starts at 0");
      CHECK(div->xml->end_positions[div->xml->total_spec - 1] == doc.len,
            "last XML span ends at document end");
      dealloc_division(div);
   }
   dealloc_df(df);
   free(doc.buf);
}

static void test_empty_spectrum_has_zero_length_binaries(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   data_format_t* df = pattern_detect(doc.buf);
   if (!df) {
      failures++;
      free(doc.buf);
      return;
   }
   division_t* div = scan_mzml(doc.buf, df, (long)doc.len, 0);
   CHECK(div != NULL, "scan_mzml must tolerate an empty spectrum");
   if (div) {
      CHECK(div->mz->start_positions[2] == div->mz->end_positions[2],
            "empty spectrum has a zero-length m/z binary");
      CHECK(div->inten->start_positions[2] == div->inten->end_positions[2],
            "empty spectrum has a zero-length intensity binary");
      dealloc_division(div);
   }
   dealloc_df(df);
   free(doc.buf);
}

static void test_base64_arrays_roundtrip(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   data_format_t* df = pattern_detect(doc.buf);
   division_t* div = df ? scan_mzml(doc.buf, df, (long)doc.len, 0) : NULL;
   CHECK(div != NULL, "scan must succeed for roundtrip");
   if (div) {
      char out[64];
      size_t outlen;

      /* m/z of spectrum 0: 44 chars -> 32 bytes of little-endian doubles. */
      size_t s = div->mz->start_positions[0], e = div->mz->end_positions[0];
      char* b64 = malloc(e - s + 1);
      memcpy(b64, doc.buf + s, e - s);
      b64[e - s] = '\0';
      outlen = sizeof(out);
      base64_decode(b64, e - s, out, &outlen, 0);
      CHECK(outlen == 32, "m/z decodes to 32 bytes");
      CHECK(memcmp(out, g_mz0, 32) == 0, "m/z doubles survive the roundtrip");
      free(b64);

      /* Intensity of spectrum 0: 24 chars -> 16 bytes of float32. */
      s = div->inten->start_positions[0];
      e = div->inten->end_positions[0];
      b64 = malloc(e - s + 1);
      memcpy(b64, doc.buf + s, e - s);
      b64[e - s] = '\0';
      outlen = sizeof(out);
      base64_decode(b64, e - s, out, &outlen, 0);
      CHECK(outlen == 16, "intensity decodes to 16 bytes");
      CHECK(memcmp(out, g_int0, 16) == 0,
            "intensity float32s survive the roundtrip");
      free(b64);

      dealloc_division(div);
   }
   dealloc_df(df);
   free(doc.buf);
}

static void test_metadata_and_escaping(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   const char* xml = doc.buf;

   CHECK(strncmp(xml, "<?xml version=\"1.0\" encoding=\"utf-8\"?>", 38) == 0,
         "document starts with an XML declaration");
   CHECK(strstr(xml, "<mzML xmlns=\"http://psi.hupo.org/ms/mzml\"") != NULL,
         "mzML root with PSI namespace");
   CHECK(strstr(xml, "<spectrumList count=\"3\"") != NULL,
         "spectrumList count matches emitted spectra");
   CHECK(strstr(xml, "<run id=\"_20260822_QC\"") != NULL,
         "digit-leading run id is NCName-sanitized");
   CHECK(strstr(xml, "name=\"/data/QC &amp; run.raw\"") != NULL,
         "source path is XML-escaped in sourceFile");
   CHECK(strstr(xml, "accession=\"MS:1000563\"") != NULL,
         "Thermo source-file format cvParam");
   CHECK(strstr(xml, "value=\"Test Instrument X\"") != NULL,
         "instrument model emitted");
   CHECK(strstr(xml, "id=\"raw2ms\" version=\"0.1.0\"") != NULL &&
            strstr(xml, "id=\"mscompress\" version=\"") != NULL,
         "softwareList names raw2ms and mscompress with versions");

   CHECK(strstr(xml, "<spectrum id=\"scan=1\" index=\"0\" defaultArrayLength=\"4\">") != NULL,
         "spectrum 0 opening tag");
   CHECK(strstr(xml, "name=\"ms level\" value=\"1\"") != NULL &&
            strstr(xml, "name=\"ms level\" value=\"2\"") != NULL,
         "ms levels emitted");
   CHECK(strstr(xml, "accession=\"MS:1000130\" name=\"positive scan\"") != NULL,
         "positive polarity cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000129\" name=\"negative scan\"") != NULL,
         "negative polarity cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000128\" name=\"profile spectrum\"") != NULL,
         "profile peak mode cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000127\" name=\"centroid spectrum\"") != NULL,
         "centroid peak mode cvParam");
   CHECK(strstr(xml, "name=\"total ion current\" value=\"12.000000\"") != NULL,
         "TIC computed from intensities");
   CHECK(strstr(xml, "name=\"base peak m/z\" value=\"103.500000\"") != NULL,
         "base peak m/z computed");
   CHECK(strstr(xml, "name=\"base peak intensity\" value=\"4.500000\"") != NULL,
         "base peak intensity computed");
   CHECK(strstr(xml, "name=\"lowest observed m/z\" value=\"100.500000\"") != NULL,
         "lowest observed m/z computed");
   CHECK(strstr(xml, "name=\"scan start time\" value=\"0.500000\"") != NULL &&
            strstr(xml, "unitAccession=\"UO:0000010\"") != NULL,
         "scan start time in seconds");
   CHECK(strstr(xml, "name=\"ion injection time\" value=\"5.000000\"") != NULL,
         "injection time emitted when present");

   CHECK(strstr(xml, "name=\"isolation window target m/z\" value=\"456.789100\"") != NULL,
         "isolation window target emitted");
   CHECK(strstr(xml, "name=\"isolation window lower offset\" value=\"0.500000\"") != NULL,
         "isolation lower offset from absolute bound");
   CHECK(strstr(xml, "name=\"isolation window upper offset\" value=\"0.500000\"") != NULL,
         "isolation upper offset from absolute bound");
   CHECK(strstr(xml, "name=\"selected ion m/z\" value=\"456.789100\"") != NULL,
         "selected ion m/z emitted");
   CHECK(strstr(xml, "name=\"charge state\" value=\"2\"") != NULL,
         "precursor charge emitted");
   CHECK(strstr(xml, "accession=\"MS:1000044\"") != NULL,
         "activation placeholder emitted");

   CHECK(strstr(xml, "<spectrum id=\"scan=3\" index=\"2\" defaultArrayLength=\"0\">") != NULL,
         "empty spectrum opening tag");
   CHECK(strstr(xml, "<binary></binary>") != NULL,
         "empty spectrum keeps empty binary elements (scanner requirement)");

   CHECK(strstr(xml, "accession=\"MS:1000514\" name=\"m/z array\"") != NULL,
         "m/z array cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000523\" name=\"64-bit float\"") != NULL,
         "64-bit float cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000515\" name=\"intensity array\"") != NULL,
         "intensity array cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000521\" name=\"32-bit float\"") != NULL,
         "32-bit float cvParam");
   CHECK(strstr(xml, "accession=\"MS:1000576\" name=\"no compression\"") != NULL,
         "no-compression cvParam");
   CHECK(strstr(xml, "<binaryDataArray encodedLength=\"44\">") != NULL,
         "encodedLength attribute on m/z array");
   CHECK(strstr(xml, "<binaryDataArray encodedLength=\"24\">") != NULL,
         "encodedLength attribute on intensity array");

   size_t len = strlen(xml);
   CHECK(len + 1 <= doc.len + 1 && doc.buf[doc.len] == '\0' &&
             strcmp(xml + doc.len, "") == 0 && doc.len == len,
         "buffer is NUL-terminated at len");

   free(doc.buf);
}

static void test_mzml_is_well_formed_xml(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   yxml_t* xml = alloc_yxml();
   int bad = 0;
   for (size_t i = 0; i < doc.len; i++) {
      if (yxml_parse(xml, doc.buf[i]) < 0) {
         bad = 1;
         break;
      }
   }
   /* This yxml build rejects a trailing NUL byte, so completeness is "every
    * byte parsed without error" — the same bar pattern_detect() applies. */
   CHECK(!bad, "yxml parses the whole document without error");
   free(xml);
   free(doc.buf);
}

static void test_fd_writer_matches_memory_writer(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   char tmpl[] = "/tmp/raw_writer_test_XXXXXX";
   int fd = mkstemp(tmpl);
   CHECK(fd >= 0, "mkstemp for fd sink");
   if (fd < 0) {
      free(doc.buf);
      return;
   }

   raw_run_t run;
   build_run(&run);
   int rc = raw_write_mzml_fd(&run, fd);
   CHECK(rc == 0, "raw_write_mzml_fd succeeds");

   /* Read it back and compare byte-for-byte with the memory writer. */
   struct stat st;
   fstat(fd, &st);
   CHECK((size_t)st.st_size == doc.len, "fd writer wrote the same length");
   char* disk = malloc(st.st_size ? st.st_size : 1);
   lseek(fd, 0, SEEK_SET);
   ssize_t got = read(fd, disk, st.st_size);
   CHECK(got == (ssize_t)st.st_size, "read back full document");
   CHECK(memcmp(disk, doc.buf, doc.len) == 0,
         "fd and memory writers produce identical bytes");

   free(disk);
   close(fd);
   unlink(tmpl);
   free(doc.buf);
}

static void test_generated_mzml_compresses_via_pipeline(void) {
   mzml_buf_t doc;
   if (write_doc(&doc))
      return;

   /* Materialize to a file so the pipeline can mmap it, exactly like the
    * chunked path will. */
   char in_tmpl[] = "/tmp/raw_pipe_in_XXXXXX";
   char out_tmpl[] = "/tmp/raw_pipe_out_XXXXXX";
   int in_fd = mkstemp(in_tmpl);
   int out_fd = mkstemp(out_tmpl);
   CHECK(in_fd >= 0 && out_fd >= 0, "temp files for pipeline test");
   if (in_fd < 0 || out_fd < 0) {
      free(doc.buf);
      return;
   }
   ssize_t w = write(in_fd, doc.buf, doc.len + 1); /* include NUL */
   CHECK(w == (ssize_t)doc.len + 1, "wrote generated mzML to temp file");

   char* map = get_mapping(in_fd);
   CHECK(map != NULL, "mmap generated mzML");

   Arguments args;
   init_args(&args);
   args.threads = 1;
   args.blocksize = 1024 * 1024;

   data_format_t* df = NULL;
   divisions_t* divisions = NULL;
   int rc = preprocess_mzml(map, (long)doc.len, &args.blocksize, &args, &df,
                            &divisions);
   CHECK(rc == 0, "preprocess_mzml accepts the generated document");
   if (rc == 0) {
      rc = compress_mzml(map, doc.len, &args, df, divisions, out_fd);
      CHECK(rc == 0, "compress_mzml compresses the generated document");
   }

   if (rc == 0) {
      struct stat st;
      fstat(out_fd, &st);
      CHECK(st.st_size > 512, "output msz has a header and streams");
      char* out_map = get_mapping(out_fd);
      footer_t* footer = out_map ? read_footer(out_map, (long)st.st_size) : NULL;
      CHECK(footer != NULL, "output msz has a readable footer");
      if (footer) {
         CHECK(footer->original_filesize == doc.len,
               "footer records the original mzML size");
         /* footer points into the mmap, not to a malloc'd struct. */
      }
      if (out_map)
         remove_mapping(out_map, st.st_size);
   }

   if (divisions)
      dealloc_divisions(divisions);
   if (df)
      dealloc_df(df);
   remove_mapping(map, doc.len + 1);
   close(in_fd);
   close(out_fd);
   unlink(in_tmpl);
   unlink(out_tmpl);
   free(doc.buf);
}

static void test_estimate_formula(void) {
   CHECK(raw_estimate_mzml_bytes(1000, 10) == 3 * 1000 + 1024 * 10,
         "estimate = 3x raw bytes + 1KB per spectrum");
   CHECK(raw_estimate_mzml_bytes(0, 0) == 0, "empty input estimates to zero");
}

static void test_should_chunk_boundary(void) {
   CHECK(raw_should_chunk(61, 100) == 1, "61% of available -> chunk");
   CHECK(raw_should_chunk(60, 100) == 0, "60% of available -> in memory");
   CHECK(raw_should_chunk(1, 100) == 0, "tiny estimate -> in memory");
   CHECK(raw_should_chunk(100, 0) == 1,
         "unknown availability -> chunk (safe default)");
}

static void test_is_raw_vendor_path(void) {
   CHECK(is_raw_vendor_path("a.RAW") == 1, ".RAW matches case-insensitively");
   CHECK(is_raw_vendor_path("/x/y.raw") == 1, ".raw path matches");
   CHECK(is_raw_vendor_path("bundle.d") == 1, "Bruker .d directory matches");
   CHECK(is_raw_vendor_path("a.wiff") == 1, ".wiff matches");
   CHECK(is_raw_vendor_path("a.wiff2") == 1, ".wiff2 matches");
   CHECK(is_raw_vendor_path("a.t2d") == 1, ".t2d matches");
   CHECK(is_raw_vendor_path("a.WiFf") == 1, ".WiFf matches");
   CHECK(is_raw_vendor_path("a.mzML") == 0, ".mzML is not raw");
   CHECK(is_raw_vendor_path("a.msz") == 0, ".msz is not raw");
   CHECK(is_raw_vendor_path("a.msZX") == 0, ".msZX is not raw");
   CHECK(is_raw_vendor_path("a.txt") == 0, ".txt is not raw");
   CHECK(is_raw_vendor_path("rawfile") == 0, "no extension is not raw");
   CHECK(is_raw_vendor_path("a.raw.bak") == 0, "trailing .bak is not raw");
   CHECK(is_raw_vendor_path(NULL) == 0, "NULL path is not raw");
}

int main(void) {
   RUN_TEST(test_pattern_detect_reads_generated_mzml);
   RUN_TEST(test_scan_mzml_locates_spectra_and_binaries);
   RUN_TEST(test_empty_spectrum_has_zero_length_binaries);
   RUN_TEST(test_base64_arrays_roundtrip);
   RUN_TEST(test_metadata_and_escaping);
   RUN_TEST(test_mzml_is_well_formed_xml);
   RUN_TEST(test_fd_writer_matches_memory_writer);
   RUN_TEST(test_generated_mzml_compresses_via_pipeline);
   RUN_TEST(test_estimate_formula);
   RUN_TEST(test_should_chunk_boundary);
   RUN_TEST(test_is_raw_vendor_path);

   printf("%d checks, %d failures\n", checks, failures);
   return failures ? 1 : 0;
}
