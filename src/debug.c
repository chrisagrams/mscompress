#include <stdio.h>
#include <stdlib.h>

#include "mscompress.h"

/**
 * @brief Dumps division position data to a file descriptor for debugging.
 * @param ddp An array of data_positions_t pointers, one per division.
 * @param divisions The number of divisions in ddp.
 * @param threads The number of threads (unused in current implementation).
 * @param fd The file descriptor to write debug output to.
 */
void dump_divisions_to_file(data_positions_t** ddp, int divisions, int threads,
                            int fd) {
   // int i = 0;
   // int j;
   // for (i; i < divisions; i++)
   // {
   //     print(fd, "=== DIVISION %d ===\n", i);
   //     j = 0;
   //     for(j; j < ddp[i]->total_spec; j++)
   //     {
   //         print(fd, "(%d, %d)\n", ddp[i]->start_positions[j],
   //         ddp[i]->end_positions[j]);
   //     }
   // }
   return;
}