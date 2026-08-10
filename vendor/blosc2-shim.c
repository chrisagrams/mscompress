/**
 * @file blosc2-shim.c
 * @brief Fills the one gap left by vendoring only c-blosc2's shuffle filters.
 *
 * `blosc2.h` defines a *static* `print_error()` that calls
 * `blosc2_error_string()`. Every translation unit including that header - both
 * blosc2's own shuffle sources and our `src/algos/shuffle.c` - therefore
 * carries a copy. `blosc2_error_string()` itself lives in `blosc2.c`, which
 * `Blosc2Include.cmake` deliberately does not build, since that file drags in
 * the frames, schunks and compressors we do not want.
 *
 * At -O2 and above the compiler discards the unreferenced static and no symbol
 * reference survives, so the link succeeds by accident. At -O0 the function
 * body is emitted and the link fails with an undefined reference. That made
 * `cmake -DENABLE_DEBUG_SYMBOLS=ON` and the Python `MSCOMPRESS_DEBUG=1` build
 * unbuildable while release builds were fine.
 *
 * Defining the symbol here makes every optimization level link. The strings
 * match blosc2.c so a trace is identical to the upstream one; the codes we can
 * actually reach are only the shuffle filters' own failure paths, which
 * blosc2's sources describe as "the impossible happened".
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
