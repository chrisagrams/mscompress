#include <stdlib.h>
#include <string.h>

#include "mscompress.h"
#include "algos/algos.h"

/*
    @section Algorithm registry
*/

const algo_info_t algo_registry[] = {
   {"cast",     _cast_64_to_32_,       TARGET_MZ,                "Cast 64-bit double to 32-bit float",            0,          0,    0},
   {"cast16",   _cast_64_to_16_,       TARGET_MZ,                "Cast 64-bit double to 16-bit float",            11.801,     0,    0},
   {"delta16",  _delta16_transform_,   TARGET_MZ,                "Delta encoding with 16-bit precision",          127.998,    0,    0},
   {"delta24",  _delta24_transform_,   TARGET_MZ,                "Delta encoding with 24-bit precision",          65536,      0,    0},
   {"delta32",  _delta32_transform_,   TARGET_MZ,                "Delta encoding with 32-bit precision",          262144.0,   0,    0},
   {"bitpack",  _bitpack_,             TARGET_MZ,                "Bit packing transform",                         10000.0,    0,    0},
   {"log",      _log2_transform_,      TARGET_INT,               "Log2 transform",                                0,          72.0, 0},
   {"vbr",      _vbr_,                 TARGET_MZ | TARGET_INT,   "Variable bit rate encoding",                    0.1,        1.0,  0},
   {"vdelta16", _vdelta16_transform_,  TARGET_MZ,                "Variable delta encoding 16-bit (experimental)", 0,          0,    1},
   {"vdelta24", _vdelta24_transform_,  TARGET_MZ,                "Variable delta encoding 24-bit (experimental)", 0,          0,    1},
};

const int algo_registry_size = sizeof(algo_registry) / sizeof(algo_registry[0]);

/*
    @section Algo switch
*/

/**
 * @brief Returns the appropriate compression algorithm function pointer based on the provided algorithm and accession type.
 * @param algo The compression algorithm type.
 * @param accession The data type accession (e.g., `32f` for 32-bit float, `64d` for 64-bit double).
 * @return A function pointer to the corresponding compression algorithm. If the algorithm or accession type is unknown, it returns `NULL` and logs an error.
 */
Algo set_compress_algo(int algo, int accession) {
   switch (algo) {
      case _lossless_:
         return algo_decode_lossless;
      case _log2_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_log_2_transform_32f;
            case _64d_:
               return algo_decode_log_2_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for log2_transform: %d\n", accession);
               return NULL;
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast32_64d;
            case _32f_:
               return algo_decode_lossless;  // casting 32 to 32 is just
                                             // lossless
            default:
               error("set_compress_algo: Unknown accession for cast_64_to_32: %d\n", accession);
               return NULL;
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast16_64d;
            case _32f_:
               return algo_decode_cast16_32f;
            default:
               error("set_compress_algo: Unknown accession for cast_64_to_16: %d\n", accession);
               return NULL;
         }
      };
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta16_transform_32f;
            case _64d_:
               return algo_decode_delta16_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for delta16_transform: %d\n", accession);
               return NULL;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta24_transform_32f;
            case _64d_:
               return algo_decode_delta24_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for delta24_transform: %d\n", accession);
               return NULL;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta32_transform_32f;
            case _64d_:
               return algo_decode_delta32_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for delta32_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta16_transform_32f;
            case _64d_:
               return algo_decode_vdelta16_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for vdelta16_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta24_transform_32f;
            case _64d_:
               return algo_decode_vdelta24_transform_64d;
            default:
               error("set_compress_algo: Unknown accession for vdelta24_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vbr_32f;
            case _64d_:
               return algo_decode_vbr_64d;
            default:
               error("set_compress_algo: Unknown accession for vbr: %d\n", accession);
               return NULL;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_decode_bitpack_32f;
            case _64d_:
               return algo_decode_bitpack_64d;
            default:
               error("set_compress_algo: Unknown accession for bitpack: %d\n", accession);
               return NULL;
         }
      };
      default:
         error("set_compress_algo: Unknown compression algorithm");
         return NULL;
   }
}

/**
 * @brief Returns the appropriate decompression algorithm function pointer based on the provided algorithm and accession type.
 * @param algo The compression algorithm type.
 * @param accession The data type accession (e.g., `32f` for 32-bit float, `64d` for 64-bit double).
 * @return A function pointer to the corresponding decompression algorithm. If the algorithm or accession type is unknown, it returns `NULL` and logs an error.
 */
Algo set_decompress_algo(int algo, int accession) {
   switch (algo) {
      case _lossless_:
         return algo_encode_lossless;
      case _log2_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_log_2_transform_32f;
            case _64d_:
               return algo_encode_log_2_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for log2_transform: %d\n", accession);
               return NULL;
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast32_64d;
            case _32f_:
               return algo_encode_lossless;  // casting 32 to 32 is just
                                             // lossless
            default:
               error("set_decompress_algo: Unknown accession for cast_64_to_32: %d\n", accession);
               return NULL;
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast16_64d;
            case _32f_:
               return algo_encode_cast16_32f;
            default:
               error("set_decompress_algo: Unknown accession for cast_64_to_16: %d\n", accession);
               return NULL;
         }
      }
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta16_transform_32f;
            case _64d_:
               return algo_encode_delta16_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for delta16_transform: %d\n", accession);
               return NULL;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta24_transform_32f;
            case _64d_:
               return algo_encode_delta24_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for delta24_transform: %d\n", accession);
               return NULL;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta32_transform_32f;
            case _64d_:
               return algo_encode_delta32_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for delta32_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta16_transform_32f;
            case _64d_:
               return algo_encode_vdelta16_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for vdelta16_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta24_transform_32f;
            case _64d_:
               return algo_encode_vdelta24_transform_64d;
            default:
               error("set_decompress_algo: Unknown accession for vdelta24_transform: %d\n", accession);
               return NULL;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vbr_32f;
            case _64d_:
               return algo_encode_vbr_64d;
            default:
               error("set_decompress_algo: Unknown accession for vbr: %d\n", accession);
               return NULL;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_encode_bitpack_32f;
            case _64d_:
               return algo_encode_bitpack_64d;
            default:
               error("set_decompress_algo: Unknown accession for bitpack: %d\n", accession);
               return NULL;
         }
      };
      default:
         error("set_decompress_algo: Unknown compression algorithm");
         return NULL;
   }
}

/**
 * @brief Returns the algorithm type based on the provided argument.
 * @param arg The argument representing the algorithm type.
 * @return An integer representing the algorithm type. If the argument is `NULL` or unknown, it logs an error and returns -1.
 */
int get_algo_type(const char* arg) {
   if (arg == NULL)
      error("get_algo_type: arg is NULL");
   if (strcmp(arg, "lossless") == 0 || *arg == '\0')
      return _lossless_;
   for (int i = 0; i < algo_registry_size; i++) {
      if (strcmp(arg, algo_registry[i].name) == 0)
         return algo_registry[i].type;
   }
   error("get_algo_type: Unknown compression algorithm");
   return -1;
}

/**
 * @brief Grows `*buff` (doubling, or to `need` if larger) so it holds at least
 *        `need` bytes.
 * @return 0 on success, 1 if realloc fails (`*buff` is left untouched).
 */
int grow_buff(char** buff, size_t* cap, size_t need) {
   if (need <= *cap) return 0;
   size_t new_cap = *cap * 2;
   if (new_cap < need) new_cap = need;
   char* grown = realloc(*buff, new_cap);
   if (grown == NULL) return 1;
   *buff = grown;
   *cap = new_cap;
   return 0;
}

/**
 * @brief Runs `target_fun` to encode one spectrum's binary at `*buff + off`,
 *        growing `*buff` and retrying once when the encoder reports the
 *        output does not fit (encoders take `*dest_len` as the room left in
 *        dest and report the size they need).
 * @param src The binary cursor `a_args->src` points at; rewound on retry.
 * @return 0 on success (`*a_args->dest_len` holds the bytes written), 1 on
 *         error, including an encoder that still does not fit after the
 *         buffer was grown to the size it asked for.
 */
int encode_into_buff(Algo target_fun, algo_args* a_args, char** src,
                     char** buff, size_t* cap, size_t off) {
   char* spec_src = *src;
   for (int attempt = 0; attempt < 2; attempt++) {
      size_t room = *cap - off;
      *src = spec_src;
      *a_args->dest_len = room;
      a_args->dest = (char**)(*buff + off);
      target_fun((void*)a_args);
      if (a_args->ret_code != 0) return 1;
      if (*a_args->dest_len <= room) return 0;
      if (attempt == 1) break;
      if (grow_buff(buff, cap, off + *a_args->dest_len)) return 1;
   }
   error("encode_into_buff: encoder output did not fit after growing buffer.\n");
   return 1;
}
