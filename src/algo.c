#include <string.h>

#include "mscompress.h"
#include "algos/algos.h"

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
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast32_64d;
            case _32f_:
               return algo_decode_lossless;  // casting 32 to 32 is just
                                             // lossless
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_decode_cast16_64d;
            case _32f_:
               return algo_decode_cast16_32f;
         }
      };
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta16_transform_32f;
            case _64d_:
               return algo_decode_delta16_transform_64d;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta24_transform_32f;
            case _64d_:
               return algo_decode_delta24_transform_64d;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_delta32_transform_32f;
            case _64d_:
               return algo_decode_delta32_transform_64d;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta16_transform_32f;
            case _64d_:
               return algo_decode_vdelta16_transform_64d;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vdelta24_transform_32f;
            case _64d_:
               return algo_decode_vdelta24_transform_64d;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_decode_vbr_32f;
            case _64d_:
               return algo_decode_vbr_64d;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_decode_bitpack_32f;
            case _64d_:
               return algo_decode_bitpack_64d;
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
         }
      };
      case _cast_64_to_32_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast32_64d;
            case _32f_:
               return algo_encode_lossless;  // casting 32 to 32 is just
                                             // lossless
         }
      };
      case _cast_64_to_16_: {
         switch (accession) {
            case _64d_:
               return algo_encode_cast16_64d;
            case _32f_:
               return algo_encode_cast16_32f;
         }
      }
      case _delta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta16_transform_32f;
            case _64d_:
               return algo_encode_delta16_transform_64d;
         }
      };
      case _delta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta24_transform_32f;
            case _64d_:
               return algo_encode_delta24_transform_64d;
         }
      };
      case _delta32_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_delta32_transform_32f;
            case _64d_:
               return algo_encode_delta32_transform_64d;
         }
      };
      case _vdelta16_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta16_transform_32f;
            case _64d_:
               return algo_encode_vdelta16_transform_64d;
         }
      };
      case _vdelta24_transform_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vdelta24_transform_32f;
            case _64d_:
               return algo_encode_vdelta24_transform_64d;
         }
      };
      case _vbr_: {
         switch (accession) {
            case _32f_:
               return algo_encode_vbr_32f;
            case _64d_:
               return algo_encode_vbr_64d;
         }
      };
      case _bitpack_: {
         switch (accession) {
            case _32f_:
               return algo_encode_bitpack_32f;
            case _64d_:
               return algo_encode_bitpack_64d;
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
   else if (strcmp(arg, "log") == 0)
      return _log2_transform_;
   else if (strcmp(arg, "cast") == 0)
      return _cast_64_to_32_;
   else if (strcmp(arg, "cast16") == 0)
      return _cast_64_to_16_;
   else if (strcmp(arg, "delta16") == 0)
      return _delta16_transform_;
   else if (strcmp(arg, "delta24") == 0)
      return _delta24_transform_;
   else if (strcmp(arg, "delta32") == 0)
      return _delta32_transform_;
   else if (strcmp(arg, "vdelta16") == 0)
      return _vdelta16_transform_;
   else if (strcmp(arg, "vdelta24") == 0)
      return _vdelta24_transform_;
   else if (strcmp(arg, "vbr") == 0)
      return _vbr_;
   else if (strcmp(arg, "bitpack") == 0)
      return _bitpack_;
   else {
      error("get_algo_type: Unknown compression algorithm");
      return -1;
   }
}
