#include "mscompress.h"

void
dump_divisions_to_file(data_positions_t** ddp, int divisions, int threads, int fd)
{
    int i = 0;
    int j;
    for (i; i < divisions; i++)
    {
        j = 0;
        for(j; j < ddp[i]->total_spec; j++)
        {
            dprintf(fd, "(%d, %d)\n", ddp[i]->start_positions[j], ddp[i]->end_positions[j]);
        }
    }
    return;
}