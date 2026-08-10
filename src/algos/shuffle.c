/**
 * @file shuffle.c
 * @brief HDF5-style byte shuffle transform applied ahead of block compression.
 *
 * Arrays of IEEE754 floats stored in native little-endian order interleave
 * near-constant bytes (sign, exponent, high mantissa) with high-entropy bytes
 * (low mantissa) every `elem_size` bytes. A general-purpose compressor sees a
 * noisy period and can rarely build long matches across it.
 *
 * The shuffle transposes the buffer plane-major: byte j of every element is
 * gathered into plane j, so byte 0 of all elements is contiguous, then byte 1,
 * and so on. The near-constant planes collapse into long runs the entropy coder
 * compresses very well, and the noisy planes are isolated where they cost only
 * themselves. This is the same transform behind HDF5's shuffle filter and
 * blosc's byte shuffle.
 *
 * The transform is a pure permutation of bytes: entirely lossless and exactly
 * reversible by `unshuffle_bytes()`.
 *
 * @version 0.0.1
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../vendor/c-blosc2/include/blosc2.h"
#include "../mscompress.h"

/* blosc2_shuffle()/blosc2_unshuffle() dispatch at runtime to SSE2/AVX2/NEON
 * kernels that transpose a register-sized tile at a time, so both loads and
 * stores stay contiguous instead of striding by the element width. Only their
 * shuffle filters are vendored (see vendor/Blosc2Include.cmake).
 *
 * Their byte layout is identical to the scalar reference below - byte j of
 * element i lands at j * n + i - which matters because a file written by a
 * build that took one path has to be readable by a build that takes the other.
 * cli/test covers that equivalence directly.
 *
 * blosc2 takes an int32_t length, so anything larger falls back to scalar. In
 * practice a block is bounded by --blocksize (100MB by default), well under the
 * limit, but the guard keeps an oversized block correct rather than truncated. */
#define BLOSC_MAX_LEN ((size_t)0x7fffffff)

/**
 * @brief Byte-shuffles `src` into `dst`.
 *
 * Moves byte `j` of element `i` to position `j * n + i`, where `n` is the
 * element count. Requires `len` to be an exact multiple of `elem_size`; the
 * compression pipeline guarantees this because a data block is only ever cut on
 * a spectrum boundary and every spectrum binary is a whole number of elements.
 *
 * Out-of-place by design: a transpose cannot be done in place without a scratch
 * copy, and taking the destination from the caller lets the surrounding code
 * write the result straight where it is needed instead of bouncing through a
 * temporary. It also matches the (src, dst, size, typesize) shape of the
 * hand-vectorised kernels this can be swapped for.
 *
 * @param src Source buffer.
 * @param dst Destination buffer of at least `len` bytes. Must not overlap `src`.
 * @param len Length in bytes.
 * @param elem_size Element width in bytes (4 for 32-bit, 8 for 64-bit).
 * @return 0 on success, non-zero on error.
 */
int shuffle_bytes(const void* src, void* dst, size_t len, int elem_size) {
   if (src == NULL || dst == NULL || len == 0)
      return 0; /* Nothing to do. */

   if (elem_size <= 1) {
      memcpy(dst, src, len);
      return 0;
   }

   if (len % (size_t)elem_size != 0) {
      error("shuffle_bytes: length %zu is not a multiple of element size %d.\n",
            len, elem_size);
      return 1;
   }

   if (len <= BLOSC_MAX_LEN &&
       blosc2_shuffle((int32_t)elem_size, (int32_t)len, src, dst) >= 0)
      return 0;

   const uint8_t* in = (const uint8_t*)src;
   uint8_t* out = (uint8_t*)dst;
   size_t n = len / (size_t)elem_size;

   /* Sequential writes into each plane, strided reads out of the source. */
   for (size_t j = 0; j < (size_t)elem_size; j++) {
      const uint8_t* p = in + j;
      uint8_t* q = out + j * n;
      for (size_t i = 0; i < n; i++)
         q[i] = p[i * (size_t)elem_size];
   }

   return 0;
}

/**
 * @brief Reverses `shuffle_bytes()`, writing `src` into `dst`.
 *
 * @param src Shuffled source buffer.
 * @param dst Destination buffer of at least `len` bytes. Must not overlap `src`.
 * @param len Length in bytes.
 * @param elem_size Element width in bytes; must match the value used to shuffle.
 * @return 0 on success, non-zero on error.
 */
int unshuffle_bytes(const void* src, void* dst, size_t len, int elem_size) {
   if (src == NULL || dst == NULL || len == 0)
      return 0; /* Nothing to do. */

   if (elem_size <= 1) {
      memcpy(dst, src, len);
      return 0;
   }

   if (len % (size_t)elem_size != 0) {
      error("unshuffle_bytes: length %zu is not a multiple of element size %d.\n",
            len, elem_size);
      return 1;
   }

   if (len <= BLOSC_MAX_LEN &&
       blosc2_unshuffle((int32_t)elem_size, (int32_t)len, src, dst) >= 0)
      return 0;

   const uint8_t* in = (const uint8_t*)src;
   uint8_t* out = (uint8_t*)dst;
   size_t n = len / (size_t)elem_size;

   /* Sequential reads out of each plane, strided writes back into elements. */
   for (size_t j = 0; j < (size_t)elem_size; j++) {
      const uint8_t* p = in + j * n;
      uint8_t* q = out + j;
      for (size_t i = 0; i < n; i++)
         q[i * (size_t)elem_size] = p[i];
   }

   return 0;
}

/*
 * A compressed data block is not a flat array of samples: it is a sequence of
 * `[ZLIB_TYPE payload_len][payload]` records, one per spectrum, and the decode
 * path walks it by those headers. So the transform is applied to payload bytes
 * only, never to a header, which would otherwise be transposed into the data and
 * destroy the walk.
 *
 * Shuffling each record's payload on its own would only transpose across a
 * single spectrum - typically a couple of kilobytes - which captures a fraction
 * of the available gain. Instead the payloads of a whole block are gathered into
 * one contiguous run, transposed together, and scattered back into their
 * original byte positions. The block layout and every record length are
 * therefore completely unchanged, while the entropy coder sees byte planes that
 * span the entire block.
 *
 * A block is shuffled only when every payload in it is a whole number of
 * elements. That predicate is computed from the record headers alone, which are
 * never shuffled, so the compress and decompress sides always agree without
 * storing any extra per-block state.
 */

/**
 * @brief Validates a block's record structure and totals its payload bytes.
 * @param block Pointer to the block.
 * @param block_len Length of the block in bytes.
 * @param elem_size Element width in bytes.
 * @param total Receives the summed payload length.
 * @return 1 if the block is well-formed and every payload is element-aligned,
 *         0 if it should be left untransformed, -1 on a malformed block.
 */
static int block_payload_total(const void* block, size_t block_len,
                               int elem_size, size_t* total) {
   const uint8_t* p = (const uint8_t*)block;
   size_t off = 0;
   size_t sum = 0;

   while (off + ZLIB_SIZE_OFFSET <= block_len) {
      ZLIB_TYPE payload_len;
      memcpy(&payload_len, p + off, ZLIB_SIZE_OFFSET);
      size_t plen = (size_t)payload_len;

      if (off + ZLIB_SIZE_OFFSET + plen > block_len) {
         error(
             "block_payload_total: record at offset %zu claims %zu payload "
             "bytes, past the end of a %zu byte block.\n",
             off, plen, block_len);
         return -1;
      }

      if (plen % (size_t)elem_size != 0)
         return 0; /* Not element-aligned; leave the whole block alone. */

      sum += plen;
      off += ZLIB_SIZE_OFFSET + plen;
   }

   *total = sum;
   return sum > 0 ? 1 : 0;
}

/* Copies payload bytes between a block and a packed buffer. `gather` selects
 * the direction: non-zero packs out of the block, zero writes back into it. */
static void block_payload_move(void* block, size_t block_len, uint8_t* packed,
                               int gather) {
   uint8_t* p = (uint8_t*)block;
   size_t off = 0;
   size_t pos = 0;

   while (off + ZLIB_SIZE_OFFSET <= block_len) {
      ZLIB_TYPE payload_len;
      memcpy(&payload_len, p + off, ZLIB_SIZE_OFFSET);
      size_t plen = (size_t)payload_len;

      if (plen > 0) {
         if (gather)
            memcpy(packed + pos, p + off + ZLIB_SIZE_OFFSET, plen);
         else
            memcpy(p + off + ZLIB_SIZE_OFFSET, packed + pos, plen);
         pos += plen;
      }

      off += ZLIB_SIZE_OFFSET + plen;
   }
}

/* Shared driver for both directions.
 *
 * Three passes over the payload: gather the records into one contiguous run,
 * transform it, then scatter the result straight back into the block. The
 * transform writes into its own destination rather than copying back over its
 * input, which is what keeps this at three passes instead of four. */
static int block_transform(void* block, size_t block_len, int elem_size,
                           int forward) {
   if (block == NULL || elem_size <= 1 || block_len == 0)
      return 0;

   size_t total = 0;
   int status = block_payload_total(block, block_len, elem_size, &total);

   if (status < 0)
      return 1;
   if (status == 0)
      return 0; /* Nothing to do. */

   /* One allocation split in two: the packed input and the transform output. */
   uint8_t* packed = (uint8_t*)malloc(total * 2);

   if (packed == NULL) {
      error("block_transform: malloc error.\n");
      return 1;
   }

   uint8_t* work = packed + total;

   block_payload_move(block, block_len, packed, 1);

   int rc = forward ? shuffle_bytes(packed, work, total, elem_size)
                    : unshuffle_bytes(packed, work, total, elem_size);

   if (rc == 0)
      block_payload_move(block, block_len, work, 0);

   free(packed);

   return rc;
}

/**
 * @brief Byte-shuffles every payload in a block as one contiguous run.
 * @param block Pointer to the block. Modified in place.
 * @param block_len Length of the block in bytes.
 * @param elem_size Element width in bytes.
 * @return 0 on success, non-zero on error.
 */
int shuffle_block_records(void* block, size_t block_len, int elem_size) {
   return block_transform(block, block_len, elem_size, 1);
}

/**
 * @brief Reverses `shuffle_block_records()`.
 * @param block Pointer to the block. Modified in place.
 * @param block_len Length of the block in bytes.
 * @param elem_size Element width in bytes.
 * @return 0 on success, non-zero on error.
 */
int unshuffle_block_records(void* block, size_t block_len, int elem_size) {
   return block_transform(block, block_len, elem_size, 0);
}

/**
 * @brief Maps an mzML binary data type accession to its element width in bytes.
 *
 * @param accession One of `_64d_`, `_32f_`, `_64i_`, `_32i_`.
 * @return Width in bytes, or 0 if the accession has no fixed width we shuffle.
 */
int fmt_elem_size(int accession) {
   switch (accession) {
      case _64d_:
      case _64i_:
         return 8;
      case _32f_:
      case _32i_:
         return 4;
      default:
         return 0;
   }
}
