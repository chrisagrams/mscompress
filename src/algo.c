#include <string.h>

#include "mscompress.h"
#include "algos/algos.h"

/*
    @section Algorithm registry
*/

const algo_info_t algo_registry[] = {
   {"cast",     _cast_64_to_32_,       TARGET_MZ,                "Cast 64-bit double to 32-bit float",            0,          0,    0, 0},
   {"cast16",   _cast_64_to_16_,       TARGET_MZ,                "Cast 64-bit double to 16-bit float",            11.801,     0,    0, 0},
   {"delta16",  _delta16_transform_,   TARGET_MZ,                "Delta encoding with 16-bit precision",          127.998,    0,    0, 0},
   {"delta24",  _delta24_transform_,   TARGET_MZ,                "Delta encoding with 24-bit precision",          65536,      0,    0, 0},
   {"delta32",  _delta32_transform_,   TARGET_MZ,                "Delta encoding with 32-bit precision",          262144.0,   0,    0, 0},
   {"bitpack",  _bitpack_,             TARGET_MZ,                "Bit packing transform",                         10000.0,    0,    0, 0},
   {"log",      _log2_transform_,      TARGET_INT,               "Log2 transform",                                0,          72.0, 0, 0},
   {"vbr",      _vbr_,                 TARGET_MZ | TARGET_INT,   "Variable bit rate encoding",                    0.1,        1.0,  0, 0},
   {"cast24",   _cast_64_to_24_,       TARGET_MZ,                "Cast to 24-bit fixed-point",                    10000.0,    0,    0, 0},
   {"topn",     _topn_,                TARGET_MZ | TARGET_INT,   "Top-N peak filter with 24-bit quantization",    10000.0,    10000.0, 0, 1},
   {"vdelta16", _vdelta16_transform_,  TARGET_MZ,                "Variable delta encoding 16-bit (experimental)", 0,          0,    1, 0},
   {"vdelta24", _vdelta24_transform_,  TARGET_MZ,                "Variable delta encoding 24-bit (experimental)", 0,          0,    1, 0},
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
      case _cast_64_to_24_: {
         switch (accession) {
            case _32f_:
               return algo_decode_cast24_32f;
            case _64d_:
               return algo_decode_cast24_64d;
         }
      };
      case _topn_: {
         switch (accession) {
            case _32f_:
               return algo_decode_topn_32f;
            case _64d_:
               return algo_decode_topn_64d;
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
      case _cast_64_to_24_: {
         switch (accession) {
            case _32f_:
               return algo_encode_cast24_32f;
            case _64d_:
               return algo_encode_cast24_64d;
         }
      };
      case _topn_: {
         switch (accession) {
            case _32f_:
               return algo_encode_cast24_32f;
            case _64d_:
               return algo_encode_cast24_64d;
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
 * @brief Checks if an algorithm requires peer data (e.g., m/z needing intensity).
 * @param type The algorithm type identifier.
 * @return 1 if coupled, 0 otherwise.
 */
int is_algo_coupled(int type) {
   for (int i = 0; i < algo_registry_size; i++) {
      if (algo_registry[i].type == type && algo_registry[i].coupled)
         return 1;
   }
   return 0;
}
