/**
 * @file raw_input.c
 * @brief Vendor raw file support: raw2ms (dlopen) -> mzML -> msz pipeline.
 *
 * The mzML writer mirrors the semantics of raw2ms's own converter (cvParams,
 * isolation-window offsets, NCName run ids) with one deliberate deviation:
 * zero-peak spectra keep a two-array binaryDataArrayList with empty
 * <binary></binary> elements, because mscompress's scan_mzml() requires
 * exactly two <binary> elements per spectrum.
 */

#include "raw_input.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libbase64.h"

#ifndef VERSION
#define VERSION "0.0.0" /* compile-time fallback when built standalone */
#endif

/* POSIX/Windows I/O spelling */
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#define msc_write _write
#define msc_close _close
#define msc_lseek _lseeki64
#else
#include <sys/mman.h>
#include <sys/stat.h>
#define msc_write write
#define msc_close close
#define msc_lseek lseek
#endif

/* === Output sinks ========================================================= */

/* Both sinks (growable memory buffer / file descriptor) share one writer
 * core, which guarantees byte-identical output on the in-memory and the
 * chunked path. */

typedef struct {
   int is_fd;
   int fd;      /* fd mode */
   char* buf;   /* mem mode */
   size_t len;
   size_t cap;
   int failed;
} sink_t;

static void sink_init_mem(sink_t* s) {
   s->is_fd = 0;
   s->fd = -1;
   s->buf = NULL;
   s->len = 0;
   s->cap = 0;
   s->failed = 0;
}

static void sink_init_fd(sink_t* s, int fd) {
   s->is_fd = 1;
   s->fd = fd;
   s->buf = NULL;
   s->len = 0;
   s->cap = 0;
   s->failed = 0;
}

static int sink_put(sink_t* s, const void* data, size_t len) {
   if (s->failed || len == 0)
      return s->failed ? -1 : 0;

   if (s->is_fd) {
      const char* p = (const char*)data;
      while (len > 0) {
         ssize_t w = msc_write(s->fd, p, (unsigned int)len);
         if (w < 0) {
            if (errno == EINTR)
               continue;
            s->failed = 1;
            return -1;
         }
         p += w;
         len -= (size_t)w;
      }
      return 0;
   }

   /* +1 keeps room for the terminating NUL added on finalize. */
   if (s->len + len + 1 > s->cap) {
      size_t ncap = s->cap ? s->cap : 65536;
      while (s->len + len + 1 > ncap)
         ncap *= 2;
      char* grown = (char*)realloc(s->buf, ncap);
      if (grown == NULL) {
         s->failed = 1;
         return -1;
      }
      s->buf = grown;
      s->cap = ncap;
   }
   memcpy(s->buf + s->len, data, len);
   s->len += len;
   return 0;
}

/* Formatted emission. mzML text lines are short; anything overflowing the
 * stack buffer is a programming error and fails the sink loudly. */
static int sink_printf(sink_t* s, const char* fmt, ...) {
   char tmp[1024];
   va_list ap;
   va_start(ap, fmt);
   int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
   va_end(ap);
   if (n < 0 || (size_t)n >= sizeof(tmp)) {
      s->failed = 1;
      return -1;
   }
   return sink_put(s, tmp, (size_t)n);
}

/* XML-attribute escaping: & < > " ' */
static int sink_put_escaped(sink_t* s, const char* str) {
   if (str == NULL)
      return 0;
   for (const char* p = str; *p; p++) {
      switch (*p) {
         case '&':
            if (sink_put(s, "&amp;", 5))
               return -1;
            break;
         case '<':
            if (sink_put(s, "&lt;", 4))
               return -1;
            break;
         case '>':
            if (sink_put(s, "&gt;", 4))
               return -1;
            break;
         case '"':
            if (sink_put(s, "&quot;", 6))
               return -1;
            break;
         case '\'':
            if (sink_put(s, "&#39;", 5))
               return -1;
            break;
         default:
            if (sink_put(s, p, 1))
               return -1;
            break;
      }
   }
   return 0;
}

/* base64-encode `nbytes` into the sink in fixed-size chunks so neither sink
 * ever materializes the full encoded array. The chunk size is a multiple of
 * 3, which keeps padding out of the stream until the final flush. */
#define B64_CHUNK (3 * 8192)

static int sink_put_base64(sink_t* s, const void* data, size_t nbytes) {
   struct base64_state st;
   char out[B64_CHUNK / 3 * 4];
   size_t outlen;
   const char* p = (const char*)data;

   base64_stream_encode_init(&st, 0);
   while (nbytes > 0) {
      size_t chunk = nbytes < B64_CHUNK ? nbytes : B64_CHUNK;
      base64_stream_encode(&st, p, chunk, out, &outlen);
      if (outlen > 0 && sink_put(s, out, outlen))
         return -1;
      p += chunk;
      nbytes -= chunk;
   }
   base64_stream_encode_final(&st, out, &outlen);
   if (outlen > 0 && sink_put(s, out, outlen))
      return -1;
   return 0;
}

/* === Small helpers ======================================================== */

static int is_nan(double v) { return v != v; }

/* run/@id is xs:ID (an NCName): it may not start with a digit and holds a
 * restricted character set. Real run names violate both, so sanitize. */
static void make_ncname(const char* in, char* out, size_t outsz) {
   size_t o = 0;
   if (in == NULL || *in == '\0') {
      snprintf(out, outsz, "run");
      return;
   }
   char c = in[0];
   if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
      if (o + 1 < outsz)
         out[o++] = '_';
   }
   for (const char* p = in; *p && o + 1 < outsz; p++) {
      c = *p;
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
          (c >= '0' && c <= '9') || c == '.' || c == '-') {
         out[o++] = c;
      } else {
         out[o++] = '_';
      }
   }
   out[o] = '\0';
   if (o == 0)
      snprintf(out, outsz, "run");
}

/* sourceFile format cvParam per vendor (psi-ms CV). */
static void source_file_cv(int32_t vendor, const char** acc, const char** name) {
   switch (vendor) {
      case RAW_VENDOR_THERMO:
         *acc = "MS:1000563", *name = "Thermo RAW format";
         break;
      case RAW_VENDOR_BRUKER_TIMS:
         *acc = "MS:1002817", *name = "Bruker TDF format";
         break;
      case RAW_VENDOR_SCIEX:
         *acc = "MS:1000562", *name = "ABI WIFF format";
         break;
      case RAW_VENDOR_WATERS:
         *acc = "MS:1000526", *name = "Waters raw format";
         break;
      default: /* unknown and .t2d share the generic parent term */
         *acc = "MS:1000560", *name = "mass spectrometer file format";
         break;
   }
}

/* === mzML writer ========================================================== */

static int mzml_write_header(sink_t* s, const raw_run_t* run, uint64_t count) {
   const char *src_acc, *src_name;
   source_file_cv(run->vendor, &src_acc, &src_name);

   char run_id[256];
   make_ncname(run->run_id, run_id, sizeof(run_id));

   if (sink_printf(s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>"))
      return -1;
   if (sink_printf(
           s,
           "<mzML xmlns=\"http://psi.hupo.org/ms/mzml\" "
           "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
           "xsi:schemaLocation=\"http://psi.hupo.org/ms/mzml "
           "mzML1.1.0.xsd\" version=\"1.1.0\">"))
      return -1;
   if (sink_printf(
           s,
           "<cvList count=\"2\"><cv id=\"MS\" fullName=\"Proteomics Standards "
           "Initiative Mass Spectrometry Ontology\" version=\"4.1.100\" "
           "URI=\"https://raw.githubusercontent.com/HUPO-PSI/psi-ms-CV/master/"
           "psi-ms.obo\"/><cv id=\"UO\" fullName=\"Unit Ontology\" "
           "version=\"09:04:2014\" URI=\"https://github.com/"
           "bio-ontology-research-group/unit-ontology/master/unit.obo\"/></cvList>"))
      return -1;

   /* fileContent is a manifest. The C API does not expose run-level MS
    * levels, so declare MS1 (levels unknown degrade to MS1 there too, and
    * every spectrum carries its own authoritative ms level cvParam). */
   if (count > 0) {
      if (sink_printf(
              s,
              "<fileDescription><fileContent><cvParam cvRef=\"MS\" "
              "accession=\"MS:1000579\" name=\"MS1 spectrum\" value=\"\"/>"
              "</fileContent>"))
         return -1;
   } else {
      if (sink_printf(s, "<fileDescription><fileContent>"))
         return -1;
   }
   if (sink_printf(
           s,
           "<sourceFileList count=\"1\"><sourceFile id=\"sf1\" name=\""))
      return -1;
   if (sink_put_escaped(s, run->source_path ? run->source_path : ""))
      return -1;
   if (sink_printf(
           s,
           "\" location=\"\"><cvParam cvRef=\"MS\" accession=\"%s\" name=\"%s\" "
           "value=\"\"/></sourceFile></sourceFileList></fileDescription>",
           src_acc, src_name))
      return -1;

   if (sink_printf(
           s,
           "<softwareList count=\"2\"><software id=\"raw2ms\" version=\"%s\">"
           "<cvParam cvRef=\"MS\" accession=\"MS:1000799\" name=\"custom "
           "unreleased software tool\" value=\"raw2ms\"/></software>"
           "<software id=\"mscompress\" version=\"%s\">"
           "<cvParam cvRef=\"MS\" accession=\"MS:1000799\" name=\"custom "
           "unreleased software tool\" value=\"mscompress\"/></software>"
           "</softwareList>",
           run->raw2ms_version ? run->raw2ms_version : "0",
           VERSION))
      return -1;

   if (sink_printf(s, "<instrumentConfigurationList count=\"1\">"
                      "<instrumentConfiguration id=\"IC1\"><cvParam cvRef=\"MS\" "
                      "accession=\"MS:1000031\" name=\"instrument model\" value=\""))
      return -1;
   if (sink_put_escaped(s, run->instrument_model ? run->instrument_model : ""))
      return -1;
   if (sink_printf(s, "\"/></instrumentConfiguration></instrumentConfigurationList>"))
      return -1;

   if (sink_printf(
           s,
           "<dataProcessingList count=\"1\"><dataProcessing id=\"dp1\">"
           "<processingMethod order=\"0\" softwareRef=\"raw2ms\">"
           "<cvParam cvRef=\"MS\" accession=\"MS:1000544\" name=\"Conversion to "
           "mzML\" value=\"\"/></processingMethod></dataProcessing>"
           "</dataProcessingList>"))
      return -1;

   if (sink_printf(s, "<run id=\"%s\" defaultInstrumentConfigurationRef=\"IC1\" "
                      "defaultSourceFileRef=\"sf1\">", run_id))
      return -1;
   if (sink_printf(s, "<spectrumList count=\"%llu\" defaultDataProcessingRef=\"dp1\">",
                   (unsigned long long)count))
      return -1;
   return 0;
}

static int mzml_write_spectrum(sink_t* s, const raw_spec_t* spec, uint64_t seq) {
   uint64_t n = spec->n_peaks;
   char idbuf[64];

   if (spec->native_id && spec->native_id[0]) {
      if (sink_printf(s, "<spectrum id=\""))
         return -1;
      if (sink_put_escaped(s, spec->native_id))
         return -1;
   } else {
      snprintf(idbuf, sizeof(idbuf), "index=%llu", (unsigned long long)seq);
      if (sink_printf(s, "<spectrum id=\"%s\"", idbuf))
         return -1;
   }
   if (sink_printf(s, "\" index=\"%llu\" defaultArrayLength=\"%llu\">",
                   (unsigned long long)seq, (unsigned long long)n))
      return -1;

   /* ms level 0 means "the vendor file does not say"; 1 is the honest
    * floor rather than a level nobody measured. */
   unsigned level = spec->ms_level > 0 ? spec->ms_level : 1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000511\" "
                      "name=\"ms level\" value=\"%u\"/>", level))
      return -1;
   if (level <= 1) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000579\" "
                         "name=\"MS1 spectrum\" value=\"\"/>"))
         return -1;
   } else {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000580\" "
                         "name=\"MSn spectrum\" value=\"\"/>"))
         return -1;
   }
   /* Only claim a representation/polarity the reader actually established;
    * a wrong cvParam is worse than none. */
   if (spec->peak_mode == RAW_PEAKS_CENTROID) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000127\" "
                         "name=\"centroid spectrum\" value=\"\"/>"))
         return -1;
   } else if (spec->peak_mode == RAW_PEAKS_PROFILE) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000128\" "
                         "name=\"profile spectrum\" value=\"\"/>"))
         return -1;
   }
   if (spec->polarity == RAW_POLARITY_POSITIVE) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000130\" "
                         "name=\"positive scan\" value=\"\"/>"))
         return -1;
   } else if (spec->polarity == RAW_POLARITY_NEGATIVE) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000129\" "
                         "name=\"negative scan\" value=\"\"/>"))
         return -1;
   }

   /* Summary numbers, accumulated in double like raw2ms does (float32
    * summation loses counts over a large continuum spectrum). */
   double tic = 0.0;
   uint64_t base_i = 0;
   for (uint64_t i = 0; i < n; i++) {
      tic += (double)spec->intensity[i];
      if (spec->intensity[i] > spec->intensity[base_i])
         base_i = i;
   }
   double base_mz = n > 0 ? spec->mz[base_i] : 0.0;
   double base_int = n > 0 ? (double)spec->intensity[base_i] : 0.0;
   double lo = n > 0 ? spec->mz[0] : 0.0;
   double hi = n > 0 ? spec->mz[n - 1] : 0.0;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000285\" "
                      "name=\"total ion current\" value=\"%.6f\"/>", tic))
      return -1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000504\" "
                      "name=\"base peak m/z\" value=\"%.6f\"/>", base_mz))
      return -1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000505\" "
                      "name=\"base peak intensity\" value=\"%.6f\"/>", base_int))
      return -1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000528\" "
                      "name=\"lowest observed m/z\" value=\"%.6f\"/>", lo))
      return -1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000527\" "
                      "name=\"highest observed m/z\" value=\"%.6f\"/>", hi))
      return -1;

   if (sink_printf(s, "<scanList count=\"1\"><cvParam cvRef=\"MS\" "
                      "accession=\"MS:1000795\" name=\"no combination\" value=\"\"/>"
                      "<scan instrumentConfigurationRef=\"IC1\">"))
      return -1;
   if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000016\" "
                      "name=\"scan start time\" value=\"%.6f\" unitCvRef=\"UO\" "
                      "unitAccession=\"UO:0000010\" unitName=\"second\"/>",
                   spec->rt_seconds))
      return -1;
   if (!is_nan(spec->injection_time_ms)) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000092\" "
                         "name=\"ion injection time\" value=\"%.6f\" "
                         "unitCvRef=\"UO\" unitAccession=\"UO:0000028\" "
                         "unitName=\"millisecond\"/>",
                   spec->injection_time_ms))
         return -1;
   }
   if (!is_nan(spec->drift_time_ms)) {
      if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1002476\" "
                         "name=\"ion mobility drift time\" value=\"%.6f\" "
                         "unitCvRef=\"UO\" unitAccession=\"UO:0000028\" "
                         "unitName=\"millisecond\"/>",
                   spec->drift_time_ms))
         return -1;
   }
   if (sink_printf(s, "</scan></scanList>"))
      return -1;

   /* precursorList sits between scanList and binaryDataArrayList per the
    * SpectrumType sequence in mzML1.1.0.xsd. */
   int has_precursor = !is_nan(spec->precursor_mz) ||
                       !is_nan(spec->isolation_lower) ||
                       !is_nan(spec->isolation_upper) || spec->precursor_id >= 0;
   if (has_precursor) {
      if (sink_printf(s, "<precursorList count=\"1\"><precursor>"))
         return -1;
      if (!is_nan(spec->precursor_mz)) {
         /* The ABI reports absolute window bounds; mzML wants offsets. */
         double lower = is_nan(spec->isolation_lower)
                            ? 0.0
                            : fabs(spec->precursor_mz - spec->isolation_lower);
         double upper = is_nan(spec->isolation_upper)
                            ? 0.0
                            : fabs(spec->isolation_upper - spec->precursor_mz);
         if (sink_printf(s, "<isolationWindow>"
                            "<cvParam cvRef=\"MS\" accession=\"MS:1000827\" "
                            "name=\"isolation window target m/z\" value=\"%.6f\" "
                            "unitCvRef=\"MS\" unitAccession=\"MS:1000040\" "
                            "unitName=\"m/z\"/>"
                            "<cvParam cvRef=\"MS\" accession=\"MS:1000828\" "
                            "name=\"isolation window lower offset\" "
                            "value=\"%.6f\" unitCvRef=\"MS\" "
                            "unitAccession=\"MS:1000040\" unitName=\"m/z\"/>"
                            "<cvParam cvRef=\"MS\" accession=\"MS:1000829\" "
                            "name=\"isolation window upper offset\" "
                            "value=\"%.6f\" unitCvRef=\"MS\" "
                            "unitAccession=\"MS:1000040\" unitName=\"m/z\"/>"
                            "</isolationWindow>",
                         spec->precursor_mz, lower, upper))
            return -1;
      }
      if (!is_nan(spec->precursor_mz)) {
         if (sink_printf(s, "<selectedIonList count=\"1\"><selectedIon>"
                            "<cvParam cvRef=\"MS\" accession=\"MS:1000744\" "
                            "name=\"selected ion m/z\" value=\"%.6f\" "
                            "unitCvRef=\"MS\" unitAccession=\"MS:1000040\" "
                            "unitName=\"m/z\"/>",
                         spec->precursor_mz))
            return -1;
         if (spec->charge >= 0) {
            if (sink_printf(s, "<cvParam cvRef=\"MS\" accession=\"MS:1000041\" "
                               "name=\"charge state\" value=\"%d\"/>",
                            spec->charge))
               return -1;
         }
         if (sink_printf(s, "</selectedIon></selectedIonList>"))
            return -1;
      }
      /* activation may not be empty; the C API exposes no collision energy,
    * so the neutral dissociation-method term stands in. */
      if (sink_printf(s, "<activation><cvParam cvRef=\"MS\" "
                         "accession=\"MS:1000044\" name=\"dissociation method\" "
                         "value=\"\"/></activation></precursor></precursorList>"))
         return -1;
   }

   /* Deviation from raw2ms's writer: the array list is emitted even for a
    * zero-peak spectrum, because scan_mzml() requires exactly two <binary>
    * elements per spectrum. */
   size_t mz_b64 = (size_t)(n * 8);
   mz_b64 = (mz_b64 + 2) / 3 * 4;
   size_t int_b64 = (size_t)(n * 4);
   int_b64 = (int_b64 + 2) / 3 * 4;
   if (sink_printf(s, "<binaryDataArrayList count=\"2\">"))
      return -1;
   if (sink_printf(s, "<binaryDataArray encodedLength=\"%llu\">"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000514\" "
                      "name=\"m/z array\" value=\"\" unitCvRef=\"MS\" "
                      "unitAccession=\"MS:1000040\" unitName=\"m/z\"/>"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000523\" "
                      "name=\"64-bit float\" value=\"\"/>"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000576\" "
                      "name=\"no compression\" value=\"\"/><binary>",
                   (unsigned long long)mz_b64))
      return -1;
   /* Host byte order is little-endian on every supported platform; mzML
    * binary arrays are little-endian IEEE-754. */
   if (n > 0 && sink_put_base64(s, spec->mz, (size_t)n * 8))
      return -1;
   if (sink_printf(s, "</binary></binaryDataArray>"))
      return -1;
   if (sink_printf(s, "<binaryDataArray encodedLength=\"%llu\">"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000515\" "
                      "name=\"intensity array\" value=\"\"/>"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000521\" "
                      "name=\"32-bit float\" value=\"\"/>"
                      "<cvParam cvRef=\"MS\" accession=\"MS:1000576\" "
                      "name=\"no compression\" value=\"\"/><binary>",
                   (unsigned long long)int_b64))
      return -1;
   if (n > 0 && sink_put_base64(s, spec->intensity, (size_t)n * 4))
      return -1;
   if (sink_printf(s, "</binary></binaryDataArray></binaryDataArrayList>"))
      return -1;

   if (sink_printf(s, "</spectrum>"))
      return -1;
   return 0;
}

static int mzml_write_footer(sink_t* s) {
   return sink_printf(s, "</spectrumList></run></mzML>");
}

static int raw_write_mzml(const raw_run_t* run, sink_t* s, int salvage) {
   if (run == NULL || run->provider.fetch == NULL)
      return -1;
   if (mzml_write_header(s, run, run->n_spectra))
      return -1;
   uint64_t written = 0, skipped = 0;
   for (uint64_t i = 0; i < run->n_spectra; i++) {
      const raw_spec_t* spec = run->provider.fetch(run->provider.ctx, i);
      if (spec == NULL) {
         const char* detail =
             run->provider.last_error
                 ? run->provider.last_error(run->provider.ctx)
                 : NULL;
         if (detail != NULL && *detail == '\0')
            detail = NULL;
         if (salvage) {
            /* Report the first failure's reason once, then keep going: for a
             * partially corrupt file the count, not every index, is the news. */
            if (skipped == 0) {
               warning("raw_input: salvage: spectrum %llu is unreadable%s%s; "
                       "skipping it and any further unreadable spectra.\n",
                       (unsigned long long)i, detail ? ": " : "",
                       detail ? detail : "");
            }
            skipped++;
            continue;
         }
         error("raw_input: provider failed to fetch spectrum %llu%s%s\n",
               (unsigned long long)i, detail ? ": " : "",
               detail ? detail : "");
         return -1;
      }
      /* Renumber on write, not fetch, so indices stay sequential when salvage
       * skips spectra; scan_mzml() reconciles the declared count downwards. */
      int rc = mzml_write_spectrum(s, spec, written);
      if (run->provider.release)
         run->provider.release(run->provider.ctx, spec);
      if (rc)
         return -1;
      written++;
   }
   if (salvage && skipped > 0) {
      if (written == 0) {
         error("raw_input: salvage: none of the %llu spectra could be read.\n",
               (unsigned long long)run->n_spectra);
         return -1;
      }
      warning("raw_input: salvage: wrote %llu of %llu spectra (%llu "
              "skipped; the mzML count attribute still declares %llu).\n",
              (unsigned long long)written, (unsigned long long)run->n_spectra,
              (unsigned long long)skipped,
              (unsigned long long)run->n_spectra);
   }
   if (mzml_write_footer(s))
      return -1;
   return 0;
}

int raw_write_mzml_mem(const raw_run_t* run, int salvage, mzml_buf_t* out) {
   sink_t s;
   sink_init_mem(&s);
   if (raw_write_mzml(run, &s, salvage)) {
      free(s.buf);
      return -1;
   }
   /* NUL-terminate: pattern_detect()/scan_mzml() scan with string helpers
    * and stop at a NUL (mmap'd files get one from the zero page past EOF).
    * The sink's +1 capacity margin guarantees room. */
   s.buf[s.len] = '\0';
   out->buf = s.buf;
   out->len = s.len;
   return 0;
}

int raw_write_mzml_fd(const raw_run_t* run, int salvage, int fd) {
   sink_t s;
   sink_init_fd(&s, fd);
   return raw_write_mzml(run, &s, salvage);
}

/* === OOM policy =========================================================== */

uint64_t raw_estimate_mzml_bytes(uint64_t raw_bytes, uint64_t n_spectra) {
   /* Saturating arithmetic: a wrong (huge) estimate must land on "chunk",
    * never on a silent wrap-around to a small number. */
   uint64_t est = 0;
   if (raw_bytes > UINT64_MAX / 3)
      return UINT64_MAX;
   est = raw_bytes * 3;
   if (n_spectra > (UINT64_MAX - est) / 1024)
      return UINT64_MAX;
   est += n_spectra * 1024;
   return est;
}

uint64_t raw_available_bytes(void) {
#if defined(__linux__)
   FILE* f = fopen("/proc/meminfo", "r");
   if (f != NULL) {
      char line[256];
      uint64_t kb = 0;
      while (fgets(line, sizeof(line), f)) {
         if (strncmp(line, "MemAvailable:", 13) == 0) {
            kb = strtoull(line + 13, NULL, 10);
            break;
         }
      }
      fclose(f);
      if (kb > 0)
         return kb * 1024;
   }
#endif
   long pages = sysconf(_SC_AVPHYS_PAGES);
   long page_size = sysconf(_SC_PAGESIZE);
   if (pages > 0 && page_size > 0)
      return (uint64_t)pages * (uint64_t)page_size;
   return 0;
}

int raw_should_chunk(uint64_t estimate, uint64_t available_bytes) {
   /* Unknown availability must degrade to the safe (chunked) path; the
    * 60% margin leaves room for the compressed output blocks on top of
    * the mzML buffer itself. */
   if (available_bytes == 0)
      return 1;
   return estimate > available_bytes * 3 / 5;
}

/* === Path detection ======================================================= */

static int ends_with_ci(const char* path, const char* suffix) {
   size_t plen = strlen(path), slen = strlen(suffix);
   if (plen < slen)
      return 0;
   const char* tail = path + plen - slen;
   for (size_t i = 0; i < slen; i++) {
      char a = tail[i], b = suffix[i];
      if (a >= 'A' && a <= 'Z')
         a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z')
         b = (char)(b - 'A' + 'a');
      if (a != b)
         return 0;
   }
   return 1;
}

int is_raw_vendor_path(const char* path) {
   if (path == NULL)
      return 0;
   /* Check .wiff2 before .wiff and note ".d" also matches nothing else:
    * every pattern is anchored at the end of the string. */
   return ends_with_ci(path, ".wiff2") || ends_with_ci(path, ".wiff") ||
          ends_with_ci(path, ".raw") || ends_with_ci(path, ".d") ||
          ends_with_ci(path, ".t2d");
}

/* === raw2ms runtime loading =============================================== */

/* The compiled raw2ms C library is loaded at runtime so builds stay
 * dependency-free: raw support is available exactly where the library is
 * installed, and a missing library is a clear runtime message, not a build
 * failure. */

typedef struct {
   void* handle;
   const char* (*version)(void);
   const char* (*last_error)(void);
   int64_t (*run_count)(const char* path);
   void* (*open)(const char* path, uint64_t run_index);
   void (*close)(void* run);
   int64_t (*spectrum_count)(const void* run);
   int32_t (*vendor)(const void* run);
   const char* (*run_id)(const void* run);
   const char* (*source_path)(const void* run);
   const char* (*instrument_model)(const void* run);
   void* (*spectrum)(const void* run, uint64_t index);
   void (*spectrum_free)(void* spectrum);
} raw2ms_api_t;

static raw2ms_api_t g_raw2ms;
static int g_raw2ms_tried = 0;

#ifdef _WIN32
#include <windows.h>
typedef HMODULE dl_handle_t;
#define msc_dlopen(p) LoadLibraryA(p)
#define msc_dlclose(h) FreeLibrary(h)
static void* msc_dlsym(dl_handle_t h, const char* name) {
   return (void*)GetProcAddress(h, name);
}
#else
#include <dlfcn.h>
typedef void* dl_handle_t;
#define msc_dlopen(p) dlopen(p, RTLD_NOW | RTLD_LOCAL)
#define msc_dlclose(h) dlclose(h)
#define msc_dlsym(h, n) dlsym(h, n)
#endif

/* Load and resolve the library once; returns 0 when g_raw2ms is usable. */
static int raw2ms_load(void) {
   if (g_raw2ms_tried)
      return g_raw2ms.handle == NULL ? -1 : 0;
   g_raw2ms_tried = 1;

#ifdef _WIN32
   const char* default_name = "raw2ms_capi.dll";
#elif defined(__APPLE__)
   const char* default_name = "libraw2ms_capi.dylib";
#else
   const char* default_name = "libraw2ms_capi.so";
#endif

   dl_handle_t handle = NULL;
   const char* explicit_path = getenv("RAW2MS_LIBRARY");
   if (explicit_path != NULL && explicit_path[0] != '\0') {
      handle = msc_dlopen(explicit_path);
      if (handle == NULL) {
         error("raw_input: cannot load RAW2MS_LIBRARY=%s.\n", explicit_path);
         return -1;
      }
   }
   if (handle == NULL)
      handle = msc_dlopen(default_name);
   if (handle == NULL) {
      error(
          "raw_input: vendor file support needs the compiled raw2ms library.\n"
          "\tSet RAW2MS_LIBRARY to the path of libraw2ms_capi "
          "(libraw2ms_capi.so / .dylib / raw2ms_capi.dll).\n");
      return -1;
   }

   static const char* const symbols[] = {
       "raw2ms_version",       "raw2ms_last_error",  "raw2ms_run_count",
       "raw2ms_open",          "raw2ms_close",       "raw2ms_spectrum_count",
       "raw2ms_vendor",        "raw2ms_run_id",      "raw2ms_source_path",
       "raw2ms_instrument_model", "raw2ms_spectrum", "raw2ms_spectrum_free",
   };
   void* fns[sizeof(symbols) / sizeof(symbols[0])];
   for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
      fns[i] = msc_dlsym(handle, symbols[i]);
      if (fns[i] == NULL) {
         error("raw_input: %s is missing from the raw2ms library.\n", symbols[i]);
         msc_dlclose(handle);
         return -1;
      }
   }

   g_raw2ms.handle = handle;
   g_raw2ms.version = (const char* (*)(void))fns[0];
   g_raw2ms.last_error = (const char* (*)(void))fns[1];
   g_raw2ms.run_count = (int64_t(*)(const char*))fns[2];
   g_raw2ms.open = (void* (*)(const char*, uint64_t))fns[3];
   g_raw2ms.close = (void (*)(void*))fns[4];
   g_raw2ms.spectrum_count = (int64_t (*)(const void*))fns[5];
   g_raw2ms.vendor = (int32_t (*)(const void*))fns[6];
   g_raw2ms.run_id = (const char* (*)(const void*))fns[7];
   g_raw2ms.source_path = (const char* (*)(const void*))fns[8];
   g_raw2ms.instrument_model = (const char* (*)(const void*))fns[9];
   g_raw2ms.spectrum = (void* (*)(const void*, uint64_t))fns[10];
   g_raw2ms.spectrum_free = (void (*)(void*))fns[11];
   return 0;
}

/* raw_spec_t mirrors Raw2msSpectrum field-for-field; if the raw2ms ABI ever
 * changes, this breaks the build instead of silently misreading spectra. */
_Static_assert(sizeof(raw_spec_t) == 120,
               "raw_spec_t must mirror Raw2msSpectrum (ABI)");

typedef struct {
   void* run;
} raw2ms_ctx_t;

/* The cast is the ABI mirror asserted above. */
static const raw_spec_t* raw2ms_fetch(void* vctx, uint64_t index) {
   raw2ms_ctx_t* ctx = (raw2ms_ctx_t*)vctx;
   return (const raw_spec_t*)g_raw2ms.spectrum(ctx->run, index);
}

/* raw2ms reports failures through a thread-local message; the fetch loop runs
 * on one thread, so this is the reason the last fetch returned NULL. */
static const char* raw2ms_provider_last_error(void* vctx) {
   (void)vctx;
   const char* e = g_raw2ms.last_error ? g_raw2ms.last_error() : NULL;
   return (e != NULL && e[0] != '\0') ? e : NULL;
}

static void raw2ms_release(void* vctx, const raw_spec_t* spec) {
   (void)vctx;
   g_raw2ms.spectrum_free((void*)spec);
}

/* === Size of the vendor input ============================================= */

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>

/* Sum of regular files under `path` (a vendor file contributes its own
 * size; a vendor directory such as a Waters .raw or Bruker .d contributes
 * the sum of its files). Follows no symlinks. */
static uint64_t raw_size_on_disk(const char* path) {
   struct stat st;
   if (stat(path, &st) != 0)
      return 0;
   if (!S_ISDIR(st.st_mode))
      return (uint64_t)st.st_size;

   DIR* d = opendir(path);
   if (d == NULL)
      return 0;
   uint64_t total = 0;
   struct dirent* e;
   while ((e = readdir(d)) != NULL) {
      if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
         continue;
      size_t need = strlen(path) + strlen(e->d_name) + 2;
      char* child = (char*)malloc(need);
      if (child == NULL)
         break;
      snprintf(child, need, "%s/%s", path, e->d_name);
      struct stat cs;
      if (lstat(child, &cs) == 0) {
         if (S_ISDIR(cs.st_mode))
            total += raw_size_on_disk(child);
         else if (S_ISREG(cs.st_mode))
            total += (uint64_t)cs.st_size;
      }
      free(child);
   }
   closedir(d);
   return total;
}
#else
/* Windows: file size only. An unmeasurable directory forces the safe
 * (chunked) path through a huge estimate rather than risking the malloc. */
static uint64_t raw_size_on_disk(const char* path) {
   struct _stat64 st;
   if (_stat64(path, &st) != 0)
      return st.st_mode & _S_IFDIR ? UINT64_MAX / 4 : 0;
   if (st.st_mode & _S_IFDIR)
      return UINT64_MAX / 4;
   return (uint64_t)st.st_size;
}
#endif

/* === Chunked (OOM-avoiding) sink ========================================== */

#ifdef __linux__
#include <sys/syscall.h>
#ifndef SYS_memfd_create
#define SYS_memfd_create 319 /* x86_64 */
#endif
#endif

/* An anonymous in-memory file on Linux (memfd), an unlinked temp file
 * elsewhere. The mzML is streamed into it spectrum by spectrum, then mmapped
 * so peak heap stays bounded no matter how large the run is. */
typedef struct {
   int fd;
   char* temp_path; /* non-NULL when a real temp file must be removed later */
} chunked_doc_t;

static int chunked_doc_create(chunked_doc_t* cd) {
   cd->temp_path = NULL;
#ifdef __linux__
   cd->fd = (int)syscall(SYS_memfd_create, "mscompress_mzml", 0);
   if (cd->fd >= 0)
      return 0;
#endif
#ifdef _WIN32
   /* No memfd and no mkstemp on Windows: a named temp file that is removed
    * once the mapping is gone. */
   char* name = _tempnam(NULL, "mscompress_raw_");
   if (name == NULL)
      return -1;
   cd->fd = _open(name, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
   if (cd->fd < 0) {
      free(name);
      return -1;
   }
   cd->temp_path = name;
   return 0;
#else
   const char* tmpdir = getenv("TMPDIR");
   if (tmpdir == NULL || tmpdir[0] == '\0')
      tmpdir = "/tmp";
   size_t need = strlen(tmpdir) + 32;
   cd->temp_path = (char*)malloc(need);
   if (cd->temp_path == NULL)
      return -1;
   snprintf(cd->temp_path, need, "%s/mscompress_raw_XXXXXX", tmpdir);
   cd->fd = mkstemp(cd->temp_path);
   if (cd->fd < 0) {
      free(cd->temp_path);
      cd->temp_path = NULL;
      return -1;
   }
   /* POSIX keeps an unlinked file alive through its descriptor, so the temp
    * file disappears the moment we exit. */
   unlink(cd->temp_path);
   free(cd->temp_path);
   cd->temp_path = NULL;
   return 0;
#endif
}

static void chunked_doc_destroy(chunked_doc_t* cd) {
   if (cd->fd >= 0) {
      msc_close(cd->fd);
      cd->fd = -1;
   }
   if (cd->temp_path != NULL) {
      remove(cd->temp_path);
      free(cd->temp_path);
      cd->temp_path = NULL;
   }
}

/* === Compression entry ==================================================== */

/* The standard pipeline both paths feed into: identical to the mzML COMPRESS
 * case in the CLI. */
static int run_compress_pipeline(char* map, size_t len, Arguments* arguments,
                                 int output_fd) {
   data_format_t* df = NULL;
   divisions_t* divisions = NULL;

   if (preprocess_mzml(map, (long)len, &arguments->blocksize, arguments, &df,
                       &divisions)) {
      return -1;
   }
   int rc = compress_mzml(map, len, arguments, df, divisions, output_fd);

   if (divisions != NULL)
      dealloc_divisions(divisions);
   if (df != NULL)
      dealloc_df(df);
   return rc;
}

int compress_raw(const char* input_path, uint64_t run_index, int salvage,
                 Arguments* arguments, int output_fd) {
   if (raw2ms_load() != 0)
      return -1;

   int64_t n_runs = g_raw2ms.run_count(input_path);
   if (n_runs < 0) {
      error("raw_input: cannot inspect %s: %s\n", input_path,
            g_raw2ms.last_error() ? g_raw2ms.last_error() : "unknown error");
      return -1;
   }
   if ((int64_t)run_index >= n_runs) {
      error("raw_input: --run-index %llu is out of range; file holds %lld "
            "run(s).\n",
            (unsigned long long)run_index, (long long)n_runs);
      return -1;
   }
   if (n_runs > 1)
      warning("raw_input: file holds %lld runs; converting run %llu (select "
              "another with --run-index).\n",
              (long long)n_runs, (unsigned long long)run_index);

   void* run = g_raw2ms.open(input_path, run_index);
   if (run == NULL) {
      error("raw_input: cannot open %s: %s\n", input_path,
            g_raw2ms.last_error() ? g_raw2ms.last_error() : "unknown error");
      return -1;
   }

   int rc = -1;

   int64_t n_spectra = g_raw2ms.spectrum_count(run);
   if (n_spectra < 0) {
      error("raw_input: cannot count spectra: %s\n",
            g_raw2ms.last_error() ? g_raw2ms.last_error() : "unknown error");
      goto out;
   }

   /* OOM check: build the whole mzML in memory only when it comfortably
    * fits, otherwise stream it into a memfd and mmap. MSCOMPRESS_FORCE_CHUNKED
    * overrides the decision so the chunked path stays testable on machines
    * with more RAM than any vendor file. */
   uint64_t raw_bytes = raw_size_on_disk(input_path);
   uint64_t estimate = raw_estimate_mzml_bytes(raw_bytes, (uint64_t)n_spectra);
   uint64_t available = raw_available_bytes();
   int chunked = raw_should_chunk(estimate, available);
   if (getenv("MSCOMPRESS_FORCE_CHUNKED") != NULL)
      chunked = 1;
   print("raw_input: %lld spectra, vendor bytes %llu, mzML estimate %llu, "
         "available %llu -> %s path\n",
         (long long)n_spectra, (unsigned long long)raw_bytes,
         (unsigned long long)estimate, (unsigned long long)available,
         chunked ? "chunked" : "in-memory");

   raw2ms_ctx_t ctx = {run};
   raw_run_t rr;
   memset(&rr, 0, sizeof(rr));
   rr.provider.ctx = &ctx;
   rr.provider.fetch = raw2ms_fetch;
   rr.provider.release = raw2ms_release;
   rr.provider.last_error = raw2ms_provider_last_error;
   rr.n_spectra = (uint64_t)n_spectra;
   rr.source_path = g_raw2ms.source_path(run);
   rr.run_id = g_raw2ms.run_id(run);
   rr.instrument_model = g_raw2ms.instrument_model(run);
   rr.raw2ms_version = g_raw2ms.version();
   rr.vendor = g_raw2ms.vendor(run);

   if (!chunked) {
      mzml_buf_t doc;
      if (raw_write_mzml_mem(&rr, salvage, &doc) != 0) {
         error("raw_input: failed to generate the mzML document.\n");
         goto out;
      }
      rc = run_compress_pipeline(doc.buf, doc.len, arguments, output_fd);
      free(doc.buf);
   } else {
      chunked_doc_t cd;
      if (chunked_doc_create(&cd) != 0) {
         error("raw_input: cannot create the staging file for the chunked "
               "path.\n");
         goto out;
      }
      if (raw_write_mzml_fd(&rr, salvage, cd.fd) != 0) {
         error("raw_input: failed to generate the mzML document.\n");
         chunked_doc_destroy(&cd);
         goto out;
      }
      /* +1 byte for the NUL the string-based scanners rely on; the pipeline
       * itself is told the length without it. */
      const char nul = '\0';
      if (msc_write(cd.fd, &nul, 1) != 1) {
         error("raw_input: failed to finalize the staging file.\n");
         chunked_doc_destroy(&cd);
         goto out;
      }
      off_t end = msc_lseek(cd.fd, 0, SEEK_END);
      if (end < 1) {
         error("raw_input: staging file has an invalid size.\n");
         chunked_doc_destroy(&cd);
         goto out;
      }
#ifdef _WIN32
      HANDLE raw_handle = (HANDLE)_get_osfhandle(cd.fd);
      HANDLE mapping = CreateFileMappingA(raw_handle, NULL, PAGE_READONLY, 0, 0, NULL);
      char* map = (char*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
      if (map == NULL) {
         error("raw_input: cannot map the staging file.\n");
         CloseHandle(mapping);
         chunked_doc_destroy(&cd);
         goto out;
      }
      rc = run_compress_pipeline(map, (size_t)end - 1, arguments, output_fd);
      UnmapViewOfFile(map);
      CloseHandle(mapping);
#else
      char* map = (char*)mmap(NULL, (size_t)end, PROT_READ, MAP_PRIVATE, cd.fd, 0);
      if (map == MAP_FAILED) {
         error("raw_input: cannot map the staging file: %s\n", strerror(errno));
         chunked_doc_destroy(&cd);
         goto out;
      }
      rc = run_compress_pipeline(map, (size_t)end - 1, arguments, output_fd);
      munmap(map, (size_t)end);
#endif
      chunked_doc_destroy(&cd);
   }

out:
   g_raw2ms.close(run);
   return rc;
}
