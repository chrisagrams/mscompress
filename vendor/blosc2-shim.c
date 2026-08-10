/**
 * @file blosc2-shim.c
 * @brief Supplies the one symbol vendoring only the shuffle filters leaves out.
 *
 * blosc2.h defines a static print_error() calling blosc2_error_string(), which
 * lives in blosc2.c - a file Blosc2Include.cmake deliberately does not build.
 * At -O2+ the compiler drops the unreferenced static and the link succeeds by
 * accident; at -O0 it is emitted and the link fails. Defining it here makes
 * every optimization level link. Strings match blosc2.c.
 */

#include "c-blosc2/include/blosc2.h"

const char* blosc2_error_string(int error_code) {
   switch (error_code) {
      case BLOSC2_ERROR_SUCCESS:
         return "Success";
      case BLOSC2_ERROR_FAILURE:
         return "Generic failure";
      case BLOSC2_ERROR_STREAM:
         return "Bad stream";
      case BLOSC2_ERROR_DATA:
         return "Invalid data";
      case BLOSC2_ERROR_MEMORY_ALLOC:
         return "Memory alloc/realloc failure";
      case BLOSC2_ERROR_READ_BUFFER:
         return "Not enough space to read";
      case BLOSC2_ERROR_WRITE_BUFFER:
         return "Not enough space to write";
      case BLOSC2_ERROR_INVALID_PARAM:
         return "Invalid parameter supplied to function";
      case BLOSC2_ERROR_FILTER_PIPELINE:
         return "Filter pipeline error";
      default:
         return "Unknown error";
   }
}
