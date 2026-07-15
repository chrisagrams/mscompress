#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mscompress.h"

/*
    @section Delta transform with drift-bounded checkpointing

    The Delta16/24/32 m/z lossy transforms quantize consecutive differences and
    reconstruct by cumulative summation. Three mechanisms bound the reconstruction
    error so it can no longer compound monotonically across a spectrum, nor blow up
    on a single outlying gap:

    1. Closed-loop (DPCM) delta. On COMPRESS, each delta is quantized relative to
       the *last reconstructed* value (the exact value the decoder will produce),
       not the original neighbour. Because the quantization residual is measured
       against the reconstructed reference on every step, the per-peak error stays
       within a single quantization step (< 1/scale_factor) instead of adding up.

    2. Periodic full-precision checkpoints. Every DELTA_CHECKPOINT_INTERVAL peaks a
       raw full-precision anchor (float for _32f_, double for _64d_) is stored in
       place of a quantized delta, and the running reference is reset to it. This
       bounds worst-case drift to at most K quantization steps regardless of
       spectrum length.

    3. Adaptive (clip-triggered) anchors. A gap wider than the codeword can express
       used to be clamped to the codeword maximum, leaving that one peak wildly
       wrong until the next periodic checkpoint healed it. Instead, when a delta
       *would* saturate, COMPRESS emits a SENTINEL codeword followed by a raw
       full-precision anchor and resets the running reference to the exact source
       value. This bounds the per-peak error to one quantization step everywhere,
       not just on average.

    On-wire layout (produced by algo_decode_*, consumed by algo_encode_*):

        [uint16_t len][anchor first_val][ slot_1 ][ slot_2 ] ... [ slot_{len-1} ]

    where `anchor` is a raw float (_32f_) or double (_64d_), and slot i
    (1 <= i < len) is one of:
        - a full-precision raw anchor (sizeof(anchor) bytes) when i % K == 0
          (periodic checkpoint), or
        - a SENTINEL codeword followed by a full-precision raw anchor
          (sizeof(codeword) + sizeof(anchor) bytes), or
        - a quantized delta: uint16_t (delta16), 3-byte big-endian (delta24), or
          uint32_t (delta32).

    The sentinel is the codeword's maximum value (DELTA{16,24,32}_SENTINEL). This is
    unambiguous because COMPRESS anchors rather than clamps whenever the scaled gap
    is >= that maximum, so the maximum is never emitted as a legitimate delta.

    Checkpoint positions are deterministic (i % K == 0), so no per-slot flag bytes
    are needed — the decoder recomputes them from the peak index. Adaptive anchors
    are data-dependent, hence the in-band sentinel. Slots are therefore variable
    length: both sides must walk the buffer with a moving pointer rather than index
    it by a fixed stride. The layout is internal to a single compress->decompress
    round trip (the buffer is opaque, ZSTD-compressed data), so both sides simply
    have to agree byte-for-byte.

    NOTE: these are lossy transforms; .msz files produced before this change are
    not expected to decode correctly, but a fresh compress->decompress round trip
    round-trips with drastically reduced drift.
*/

/* Checkpoint interval: one full-precision anchor every K peaks. Bounds worst-case
   drift to at most K quantization steps regardless of spectrum length. */
#define DELTA_CHECKPOINT_INTERVAL 256

/* True when peak index `i` (1 <= i < len) stores a full-precision checkpoint
   anchor instead of a quantized delta. */
#define DELTA_IS_CHECKPOINT(i) (((i) % DELTA_CHECKPOINT_INTERVAL) == 0)

/* Sentinel codeword per delta width: "a raw full-precision anchor follows".
   Each is the maximum value the codeword can hold, which COMPRESS never emits as a
   real delta -- a scaled gap >= this maximum is anchored instead of clamped. Used
   as both the saturation test on COMPRESS and the sentinel test on DECOMPRESS, so
   the two can never disagree. */
#define DELTA16_SENTINEL ((uint16_t)UINT16_MAX)
#define DELTA24_SENTINEL ((uint32_t)0xFFFFFF)  /* 16777215 */
#define DELTA32_SENTINEL ((uint32_t)UINT32_MAX)

/*
    @section Decoding functions (COMPRESS path: original array -> delta stream)
*/

/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 16-bit deltas.
 *
 * Decodes the source buffer of 32-bit floats, then closed-loop quantizes consecutive
 * differences relative to the last reconstructed value, storing each delta as a
 * `uint16_t`. Every `DELTA_CHECKPOINT_INTERVAL` peaks a raw full-precision float anchor
 * is stored instead and the running reference is reset to it. A gap too wide for the
 * codeword emits a `DELTA16_SENTINEL` codeword plus a raw float anchor rather than
 * clamping.
 *
 * The first value is preserved at full 32-bit float precision.
 *
 * Output layout: `[uint16_t len][float first_val][ per-peak slots... ]` where each slot
 * is a `uint16_t` delta, a raw `float` anchor at checkpoint positions (i % K == 0), or
 * a `DELTA16_SENTINEL` codeword followed by a raw `float` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta16_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta16_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(float);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Adaptive anchors are data-dependent, so the exact output size is not known up
   // front. Size for the worst case -- every delta slot carrying a sentinel codeword
   // plus a raw anchor -- and report the bytes actually written via *dest_len. A
   // periodic checkpoint slot (anchor only) is smaller, so this bound covers it too.
   size_t res_len_max = sizeof(uint16_t) + sizeof(float)
                      + ndeltas * (sizeof(uint16_t) + sizeof(float));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta16_transform_32f: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   float* f = (float*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(float));  // full-precision first value
   out += sizeof(float);

   // Closed-loop reference: exactly what the decoder will reconstruct.
   float recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         // Periodic checkpoint: position is deterministic, so no sentinel is needed.
         memcpy(out, &f[i], sizeof(float));
         out += sizeof(float);
         recon = f[i];
      } else {
         double scaled = (double)(f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA16_SENTINEL || scaled < 0.0) {
            // Gap is not representable: anchor adaptively instead of clamping. The
            // >= (not >) keeps the sentinel out of the legitimate delta range.
            uint16_t sentinel = DELTA16_SENTINEL;
            memcpy(out, &sentinel, sizeof(uint16_t));
            out += sizeof(uint16_t);
            memcpy(out, &f[i], sizeof(float));
            out += sizeof(float);
            recon = f[i];
         } else {
            uint16_t uint_diff = (uint16_t)floor(scaled);
            memcpy(out, &uint_diff, sizeof(uint16_t));
            out += sizeof(uint16_t);
            recon += (float)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}


/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 16-bit deltas.
 *
 * Closed-loop quantizes consecutive differences relative to the last reconstructed value,
 * storing each delta as a `uint16_t`. Every `DELTA_CHECKPOINT_INTERVAL` peaks a raw
 * full-precision double anchor is stored instead and the running reference is reset to it.
 * A gap too wide for the codeword emits a `DELTA16_SENTINEL` codeword plus a raw double
 * anchor rather than clamping.
 *
 * The first value is preserved at full 64-bit double precision.
 *
 * Output layout: `[uint16_t len][double first_val][ per-peak slots... ]` where each slot
 * is a `uint16_t` delta, a raw `double` anchor at checkpoint positions (i % K == 0), or a
 * `DELTA16_SENTINEL` codeword followed by a raw `double` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta16_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta16_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(double);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Worst-case sizing (see algo_decode_delta16_transform_32f): every delta slot
   // carrying a sentinel codeword plus a raw anchor.
   size_t res_len_max = sizeof(uint16_t) + sizeof(double)
                      + ndeltas * (sizeof(uint16_t) + sizeof(double));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta16_transform_64d: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   double* f = (double*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(double));  // full-precision first value
   out += sizeof(double);

   double recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(out, &f[i], sizeof(double));
         out += sizeof(double);
         recon = f[i];
      } else {
         double scaled = (f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA16_SENTINEL || scaled < 0.0) {
            uint16_t sentinel = DELTA16_SENTINEL;
            memcpy(out, &sentinel, sizeof(uint16_t));
            out += sizeof(uint16_t);
            memcpy(out, &f[i], sizeof(double));
            out += sizeof(double);
            recon = f[i];
         } else {
            uint16_t uint_diff = (uint16_t)floor(scaled);
            memcpy(out, &uint_diff, sizeof(uint16_t));
            out += sizeof(uint16_t);
            recon += (double)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}


/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 24-bit deltas.
 *
 * Closed-loop quantizes consecutive differences relative to the last reconstructed value,
 * storing each delta as a packed 24-bit (3-byte) big-endian unsigned integer. Every
 * `DELTA_CHECKPOINT_INTERVAL` peaks a raw full-precision float anchor is stored instead and
 * the running reference is reset. A gap too wide for the codeword emits a
 * `DELTA24_SENTINEL` codeword plus a raw float anchor rather than clamping.
 *
 * The first value is preserved at full 32-bit float precision.
 *
 * Output layout: `[uint16_t len][float first_val][ per-peak slots... ]` where each slot is
 * a 3-byte delta, a raw `float` anchor at checkpoint positions (i % K == 0), or a
 * `DELTA24_SENTINEL` codeword followed by a raw `float` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta24_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta24_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(float);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Worst-case sizing (see algo_decode_delta16_transform_32f): every delta slot
   // carrying a sentinel codeword plus a raw anchor.
   size_t res_len_max = sizeof(uint16_t) + sizeof(float)
                      + ndeltas * (3 + sizeof(float));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta24_transform_32f: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   float* f = (float*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(float));  // full-precision first value
   out += sizeof(float);

   float recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(out, &f[i], sizeof(float));
         out += sizeof(float);
         recon = f[i];
      } else {
         double scaled = (double)(f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA24_SENTINEL || scaled < 0.0) {
            out[0] = (DELTA24_SENTINEL >> 16) & 0xFF;
            out[1] = (DELTA24_SENTINEL >> 8) & 0xFF;
            out[2] = (DELTA24_SENTINEL) & 0xFF;
            out += 3;
            memcpy(out, &f[i], sizeof(float));
            out += sizeof(float);
            recon = f[i];
         } else {
            uint32_t uint_diff = (uint32_t)floor(scaled);
            out[0] = (uint_diff >> 16) & 0xFF;
            out[1] = (uint_diff >> 8) & 0xFF;
            out[2] = (uint_diff) & 0xFF;
            out += 3;
            recon += (float)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}


/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 24-bit deltas.
 *
 * Closed-loop quantizes consecutive differences relative to the last reconstructed value,
 * storing each delta as a packed 24-bit (3-byte) big-endian unsigned integer. Every
 * `DELTA_CHECKPOINT_INTERVAL` peaks a raw full-precision double anchor is stored instead and
 * the running reference is reset. A gap too wide for the codeword emits a
 * `DELTA24_SENTINEL` codeword plus a raw double anchor rather than clamping.
 *
 * The first value is preserved at full 64-bit double precision.
 *
 * Output layout: `[uint16_t len][double first_val][ per-peak slots... ]` where each slot is
 * a 3-byte delta, a raw `double` anchor at checkpoint positions (i % K == 0), or a
 * `DELTA24_SENTINEL` codeword followed by a raw `double` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta24_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta24_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(double);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Worst-case sizing (see algo_decode_delta16_transform_32f): every delta slot
   // carrying a sentinel codeword plus a raw anchor.
   size_t res_len_max = sizeof(uint16_t) + sizeof(double)
                      + ndeltas * (3 + sizeof(double));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta24_transform_64d: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   double* f = (double*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(double));  // full-precision first value
   out += sizeof(double);

   double recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(out, &f[i], sizeof(double));
         out += sizeof(double);
         recon = f[i];
      } else {
         double scaled = (f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA24_SENTINEL || scaled < 0.0) {
            out[0] = (DELTA24_SENTINEL >> 16) & 0xFF;
            out[1] = (DELTA24_SENTINEL >> 8) & 0xFF;
            out[2] = (DELTA24_SENTINEL) & 0xFF;
            out += 3;
            memcpy(out, &f[i], sizeof(double));
            out += sizeof(double);
            recon = f[i];
         } else {
            uint32_t uint_diff = (uint32_t)floor(scaled);
            out[0] = (uint_diff >> 16) & 0xFF;
            out[1] = (uint_diff >> 8) & 0xFF;
            out[2] = (uint_diff) & 0xFF;
            out += 3;
            recon += (double)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}

/**
 * @brief Decode and apply delta encoding to a 32-bit float array, producing 32-bit deltas.
 *
 * Closed-loop quantizes consecutive differences relative to the last reconstructed value,
 * storing each delta as a `uint32_t`. Every `DELTA_CHECKPOINT_INTERVAL` peaks a raw
 * full-precision float anchor is stored instead and the running reference is reset. A gap
 * too wide for the codeword emits a `DELTA32_SENTINEL` codeword plus a raw float anchor
 * rather than clamping.
 *
 * The first value is preserved at full 32-bit float precision.
 *
 * Output layout: `[uint16_t len][float first_val][ per-peak slots... ]` where each slot is a
 * `uint32_t` delta, a raw `float` anchor at checkpoint positions (i % K == 0), or a
 * `DELTA32_SENTINEL` codeword followed by a raw `float` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta32_transform_32f: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_decode_delta32_transform_32f: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(float);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Worst-case sizing (see algo_decode_delta16_transform_32f): every delta slot
   // carrying a sentinel codeword plus a raw anchor.
   size_t res_len_max = sizeof(uint16_t) + sizeof(float)
                      + ndeltas * (sizeof(uint32_t) + sizeof(float));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta32_transform_32f: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   float* f = (float*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(float));  // full-precision first value
   out += sizeof(float);

   float recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(out, &f[i], sizeof(float));
         out += sizeof(float);
         recon = f[i];
      } else {
         double scaled = (double)(f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA32_SENTINEL || scaled < 0.0) {
            uint32_t sentinel = DELTA32_SENTINEL;
            memcpy(out, &sentinel, sizeof(uint32_t));
            out += sizeof(uint32_t);
            memcpy(out, &f[i], sizeof(float));
            out += sizeof(float);
            recon = f[i];
         } else {
            uint32_t uint_diff = (uint32_t)floor(scaled);
            memcpy(out, &uint_diff, sizeof(uint32_t));
            out += sizeof(uint32_t);
            recon += (float)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}

/**
 * @brief Decode and apply delta encoding to a 64-bit double array, producing 32-bit deltas.
 *
 * Closed-loop quantizes consecutive differences relative to the last reconstructed value,
 * storing each delta as a `uint32_t`. Every `DELTA_CHECKPOINT_INTERVAL` peaks a raw
 * full-precision double anchor is stored instead and the running reference is reset. A gap
 * too wide for the codeword emits a `DELTA32_SENTINEL` codeword plus a raw double anchor
 * rather than clamping.
 *
 * The first value is preserved at full 64-bit double precision.
 *
 * Output layout: `[uint16_t len][double first_val][ per-peak slots... ]` where each slot is a
 * `uint32_t` delta, a raw `double` anchor at checkpoint positions (i % K == 0), or a
 * `DELTA32_SENTINEL` codeword followed by a raw `double` anchor (see file header).
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 * @warning The caller owns the allocated result buffer stored in `*a_args->dest`.
 *          The intermediate decoded buffer is freed internally.
 */
void algo_decode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args->src == NULL) {
      error("algo_decode_delta32_transform_64d: src is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_decode_delta32_transform_64d: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   char* decoded = NULL;
   size_t decoded_len = 0;

   // Decode using specified encoding format
   a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded,
                   &decoded_len, a_args->tmp);

   uint16_t len = decoded_len / sizeof(double);

   size_t ndeltas = (len >= 1) ? (size_t)(len - 1) : 0;

   // Worst-case sizing (see algo_decode_delta16_transform_32f): every delta slot
   // carrying a sentinel codeword plus a raw anchor.
   size_t res_len_max = sizeof(uint16_t) + sizeof(double)
                      + ndeltas * (sizeof(uint32_t) + sizeof(double));

   uint8_t* res = malloc(res_len_max);

   if (res == NULL) {
      error("algo_decode_delta32_transform_64d: malloc failed");
      free(decoded);
      a_args->ret_code = -1;
      return;
   }

   double* f = (double*)(decoded);

   uint8_t* out = res;
   memcpy(out, &len, sizeof(uint16_t));
   out += sizeof(uint16_t);
   memcpy(out, &f[0], sizeof(double));  // full-precision first value
   out += sizeof(double);

   double recon = f[0];
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(out, &f[i], sizeof(double));
         out += sizeof(double);
         recon = f[i];
      } else {
         double scaled = (f[i] - recon) * a_args->scale_factor;
         if (scaled >= (double)DELTA32_SENTINEL || scaled < 0.0) {
            uint32_t sentinel = DELTA32_SENTINEL;
            memcpy(out, &sentinel, sizeof(uint32_t));
            out += sizeof(uint32_t);
            memcpy(out, &f[i], sizeof(double));
            out += sizeof(double);
            recon = f[i];
         } else {
            uint32_t uint_diff = (uint32_t)floor(scaled);
            memcpy(out, &uint_diff, sizeof(uint32_t));
            out += sizeof(uint32_t);
            recon += (double)uint_diff / a_args->scale_factor;
         }
      }
   }

   free(decoded);

   *a_args->dest = (char*)res;
   *a_args->dest_len = (size_t)(out - res);

   return;
}

/*
    @section Encoding functions (DECOMPRESS path: delta stream -> original array)
*/

/**
 * @brief Reconstruct a 32-bit float array from 16-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots, reconstructing the original float array by
 * cumulative addition of `delta / scale_factor`, resetting to the raw full-precision anchor
 * at checkpoint positions (i % K == 0) and at `DELTA16_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][float start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta16_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   float start;
   memcpy(&start, in, sizeof(float));
   in += sizeof(float);

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   float recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(float));
         in += sizeof(float);
      } else {
         uint16_t d;
         memcpy(&d, in, sizeof(uint16_t));
         in += sizeof(uint16_t);
         if (d == DELTA16_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(float));
            in += sizeof(float);
         } else {
            recon += (float)d / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}

/**
 * @brief Reconstruct a 64-bit double array from 16-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots, reconstructing the original double array by
 * cumulative addition of `delta / scale_factor`, resetting to the raw full-precision anchor
 * at checkpoint positions (i % K == 0) and at `DELTA16_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][double start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta16_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   double start;
   memcpy(&start, in, sizeof(double));
   in += sizeof(double);

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   double recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(double));
         in += sizeof(double);
      } else {
         uint16_t d;
         memcpy(&d, in, sizeof(uint16_t));
         in += sizeof(uint16_t);
         if (d == DELTA16_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(double));
            in += sizeof(double);
         } else {
            recon += (double)d / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}

/**
 * @brief Reconstruct a 32-bit float array from 24-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots (packed 24-bit big-endian deltas),
 * reconstructing the original float array by cumulative addition of `delta / scale_factor`,
 * resetting to the raw full-precision anchor at checkpoint positions (i % K == 0) and at
 * `DELTA24_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][float start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta24_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   float start;
   memcpy(&start, in, sizeof(float));
   in += sizeof(float);

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   float recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(float));
         in += sizeof(float);
      } else {
         uint32_t value = ((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) |
                          ((uint32_t)in[2]);
         in += 3;
         if (value == DELTA24_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(float));
            in += sizeof(float);
         } else {
            recon += (float)value / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}

/**
 * @brief Reconstruct a 64-bit double array from 24-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots (packed 24-bit big-endian deltas),
 * reconstructing the original double array by cumulative addition of `delta / scale_factor`,
 * resetting to the raw full-precision anchor at checkpoint positions (i % K == 0) and at
 * `DELTA24_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][double start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta24_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   double start;
   memcpy(&start, in, sizeof(double));
   in += sizeof(double);

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   double recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(double));
         in += sizeof(double);
      } else {
         uint32_t value = ((uint32_t)in[0] << 16) | ((uint32_t)in[1] << 8) |
                          ((uint32_t)in[2]);
         in += 3;
         if (value == DELTA24_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(double));
            in += sizeof(double);
         } else {
            recon += (double)value / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}

/**
 * @brief Reconstruct a 32-bit float array from 32-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots, reconstructing the original float array by
 * cumulative addition of `delta / scale_factor`, resetting to the raw full-precision anchor
 * at checkpoint positions (i % K == 0) and at `DELTA32_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][float start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_32f_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta32_transform_32f(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _32f_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _32f_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   float start;
   memcpy(&start, in, sizeof(float));
   in += sizeof(float);

   // Allocate buffer
   size_t res_len = len * sizeof(float);
   float* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   float recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(float));
         in += sizeof(float);
      } else {
         uint32_t d;
         memcpy(&d, in, sizeof(uint32_t));
         in += sizeof(uint32_t);
         if (d == DELTA32_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(float));
            in += sizeof(float);
         } else {
            recon += (float)d / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}

/**
 * @brief Reconstruct a 64-bit double array from 32-bit delta-encoded data.
 *
 * Reads the starting value and per-peak slots, reconstructing the original double array by
 * cumulative addition of `delta / scale_factor`, resetting to the raw full-precision anchor
 * at checkpoint positions (i % K == 0) and at `DELTA32_SENTINEL` codewords.
 *
 * Input layout: `[uint16_t len][double start][ per-peak slots... ]` (see file header).
 * Slots are variable length, so the buffer is walked with a moving pointer and
 * `*a_args->src` is advanced by the exact number of bytes consumed.
 *
 * Expects `a_args->src_format` to be `_64d_`.
 *
 * @param args Pointer to `algo_args` struct containing source data and output pointers.
 * @note Errors are reported via `a_args->ret_code` (-1 on error, 0 on success).
 */
void algo_encode_delta32_transform_64d(void* args) {
   // Parse args
   algo_args* a_args = (algo_args*)args;

   if (a_args == NULL) {
      error("algo_encode_delta_transform: args is NULL");
      a_args->ret_code = -1;
      return;
   }

   if (a_args->src_format != _64d_) {
      error("algo_encode_delta_transform: Unknown data format. Expected _64d_, got %d", a_args->src_format);
      a_args->ret_code = -1;
      return;
   }

   // Get array length
   uint16_t len = *(uint16_t*)(*a_args->src);

   if (len <= 0) {
      error("algo_encode_delta_transform: len is <= 0");
      a_args->ret_code = -1;
      return;
   }

   const uint8_t* in = (const uint8_t*)(*a_args->src);
   const uint8_t* base = in;
   in += sizeof(uint16_t);

   double start;
   memcpy(&start, in, sizeof(double));
   in += sizeof(double);

   // Allocate buffer
   size_t res_len = len * sizeof(double);
   double* res = malloc(res_len);

   if (res == NULL) {
      error("algo_encode_delta_transform: malloc failed");
      a_args->ret_code = -1;
      return;
   }

   res[0] = start;
   double recon = start;
   for (uint16_t i = 1; i < len; i++) {
      if (DELTA_IS_CHECKPOINT(i)) {
         memcpy(&recon, in, sizeof(double));
         in += sizeof(double);
      } else {
         uint32_t d;
         memcpy(&d, in, sizeof(uint32_t));
         in += sizeof(uint32_t);
         if (d == DELTA32_SENTINEL) {
            // Adaptive anchor: a raw full-precision value follows the sentinel.
            memcpy(&recon, in, sizeof(double));
            in += sizeof(double);
         } else {
            recon += (double)d / a_args->scale_factor;
         }
      }
      res[i] = recon;
   }

   // Encode using specified encoding format
   a_args->enc_fun(a_args->z, (char **)(&res), res_len,
                   (char *)a_args->dest, a_args->dest_len);

   // Move to next array (slots are variable length, so advance by bytes consumed)
   *a_args->src += (in - base);

   return;
}
