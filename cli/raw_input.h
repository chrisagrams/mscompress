/**
 * @file raw_input.h
 * @brief Direct vendor raw file support: raw -> mzML (in memory) -> msz.
 *
 * Reads Thermo (.raw), Bruker timsTOF (.d), SCIEX (.wiff/.wiff2/.t2d) and
 * Waters (.raw directory) files through the compiled raw2ms C library
 * (libraw2ms_capi) loaded at runtime with dlopen()/LoadLibraryA(), then
 * synthesizes an mzML document and feeds it into the existing compression
 * pipeline (preprocess_mzml + compress_mzml).
 *
 * The mzML is materialized either fully in memory (when an OOM check says it
 * fits) or streamed spectrum-by-spectrum into a memfd/temp file that is then
 * mmapped, so peak heap usage stays bounded on large runs.
 */

#ifndef RAW_INPUT_H
#define RAW_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "mscompress.h"

/* Vendor codes mirrored from the raw2ms C ABI (RAW2MS_VENDOR_*). */
#define RAW_VENDOR_UNKNOWN 0
#define RAW_VENDOR_THERMO 1
#define RAW_VENDOR_BRUKER_TIMS 2
#define RAW_VENDOR_SCIEX 3
#define RAW_VENDOR_WATERS 4
#define RAW_VENDOR_SCIEX_T2D 5

/* Polarity / peak-mode codes mirrored from the raw2ms C ABI. */
#define RAW_POLARITY_POSITIVE 1
#define RAW_POLARITY_NEGATIVE (-1)
#define RAW_PEAKS_CENTROID 1
#define RAW_PEAKS_PROFILE 2

/**
 * @brief One decoded spectrum.
 *
 * Field order and types mirror Raw2msSpectrum in the raw2ms C ABI — keep in
 * step. Absent floating-point values are NaN; absent ids are -1.
 */
typedef struct {
   uint64_t index;
   uint8_t ms_level;           /* 0 when the vendor file does not say */
   int8_t polarity;            /* RAW_POLARITY_* */
   int8_t peak_mode;           /* RAW_PEAKS_* */
   double rt_seconds;
   double drift_time_ms;       /* NaN unless per-spectrum IM (Waters) */
   double injection_time_ms;   /* NaN when unavailable */
   double precursor_mz;        /* NaN when not MS/MS or no single ion */
   double isolation_lower;     /* NaN when there is no window */
   double isolation_upper;
   int64_t precursor_id;       /* -1 when absent */
   int32_t charge;             /* -1 when absent */
   uint64_t n_peaks;
   const double* mz;           /* n_peaks entries, ascending */
   const float* intensity;     /* n_peaks entries */
   const float* mobility;      /* n_peaks entries or NULL (not emitted) */
   const char* native_id;      /* vendor-native id, NUL-terminated */
} raw_spec_t;

/**
 * @brief Spectra are pulled one at a time so neither the in-memory nor the
 *        chunked sink ever holds more than one spectrum's mzML text.
 */
typedef struct {
   void* ctx;
   const raw_spec_t* (*fetch)(void* ctx, uint64_t index); /* NULL on error */
   void (*release)(void* ctx, const raw_spec_t* spec);    /* NULL if static */
} raw_provider_t;

/**
 * @brief A vendor run plus the metadata the mzML header needs.
 */
typedef struct {
   raw_provider_t provider;
   uint64_t n_spectra;
   const char* source_path;      /* vendor file the run was read from */
   const char* run_id;           /* may be NULL */
   const char* instrument_model; /* may be NULL or "" */
   const char* raw2ms_version;   /* version of the reader, may be NULL */
   int32_t vendor;               /* RAW_VENDOR_* */
} raw_run_t;

/**
 * @brief In-memory mzML produced by raw_write_mzml_mem().
 *
 * buf is malloc'd and NUL-terminated at buf[len]; pattern_detect() and
 * scan_mzml() rely on that terminator (mmap'd inputs get it from the
 * zero page past EOF, a malloc'd buffer must provide it). Free with free().
 */
typedef struct {
   char* buf;
   size_t len;
} mzml_buf_t;

/**
 * @brief Write `run` as a complete mzML 1.1.0 document into a growable
 *        memory buffer.
 * @param run The run to serialize.
 * @param out Receives the malloc'd buffer and length on success.
 * @return 0 on success, -1 on allocation/provider failure.
 */
int raw_write_mzml_mem(const raw_run_t* run, mzml_buf_t* out);

/**
 * @brief Write `run` as a complete mzML 1.1.0 document to an open file
 *        descriptor (memfd or temp file), one spectrum at a time.
 *
 * Used by the chunked (OOM-avoiding) path: the caller mmaps the fd
 * afterwards and runs the standard pipeline over the mapping.
 *
 * @param run The run to serialize.
 * @param fd Open, writable file descriptor positioned at the start.
 * @return 0 on success, -1 on failure.
 */
int raw_write_mzml_fd(const raw_run_t* run, int fd);

/**
 * @brief Estimate the mzML size a raw input will expand to.
 *
 * Peak data dominates: vendors store ~4-8 bytes/peak, the mzML carries
 * ~16 bytes/peak of base64 (12 raw bytes for double m/z + float intensity),
 * so ~3x the on-disk size is a conservative figure. XML metadata adds a
 * per-spectrum constant.
 *
 * @param raw_bytes Size of the vendor file (or the sum of a vendor
 *                  directory's files) on disk.
 * @param n_spectra Spectrum count of the run.
 * @return The estimate in bytes.
 */
uint64_t raw_estimate_mzml_bytes(uint64_t raw_bytes, uint64_t n_spectra);

/**
 * @brief Bytes currently available for allocation without swapping hard.
 *
 * Reads MemAvailable from /proc/meminfo on Linux; falls back to
 * sysconf(_SC_AVPHYS_PAGES) * page size elsewhere. Returns 0 when it cannot
 * be determined (callers then default to the chunked path).
 */
uint64_t raw_available_bytes(void);

/**
 * @brief OOM check deciding between the in-memory and the chunked path.
 * @param estimate raw_estimate_mzml_bytes() result.
 * @param available_bytes raw_available_bytes() result.
 * @return 1 when the mzML should be built in chunks (estimate exceeds 60% of
 *         what is available), 0 when it is safe to build in memory.
 */
int raw_should_chunk(uint64_t estimate, uint64_t available_bytes);

/**
 * @brief True when `path` looks like a supported vendor raw file by
 *        extension: .raw (Thermo file / Waters directory), .d (Bruker),
 *        .wiff, .wiff2, .t2d. Case-insensitive.
 */
int is_raw_vendor_path(const char* path);

/**
 * @brief Compress a vendor raw file directly to msz on `output_fd`.
 *
 * Loads libraw2ms_capi at runtime, converts run `run_index` of the file to
 * an mzML (in memory when the OOM check passes, memfd/temp-file chunked
 * otherwise) and runs the existing preprocess_mzml + compress_mzml pipeline.
 * All standard compression flags in `arguments` apply.
 *
 * @param input_path Path to the vendor file.
 * @param run_index Run to convert inside multi-run containers (.wiff).
 * @param arguments Populated CLI arguments.
 * @param output_fd Open output .msz file descriptor.
 * @return 0 on success, -1 on failure (message already printed).
 */
int compress_raw(const char* input_path, uint64_t run_index,
                 Arguments* arguments, int output_fd);

#endif /* RAW_INPUT_H */
