#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mscompress.h"

void
extract_mzml(char* input_map, divisions_t* divisions, int output_fd)
{
    for (int i = 0; i < divisions->n_divisions; i++)
    {
        division_t* division = divisions->divisions[i];

        long total_spec = division->mz->total_spec;

        long j = 0;

        char* buff = malloc(division->size);

        long len = 0;

        long out_len = 0;

        int64_t xml_i = 0,   mz_i = 0,   inten_i = 0;

        while(j <= total_spec)
        {
            //XML
            len = division->xml->end_positions[xml_i] - division->xml->start_positions[xml_i];
            memcpy(buff+out_len, input_map+division->xml->start_positions[xml_i], len);
            out_len += len;
            xml_i++;

            //mz
            len = division->mz->end_positions[mz_i] - division->mz->start_positions[mz_i];
            memcpy(buff+out_len, input_map+division->mz->start_positions[mz_i], len);
            out_len += len;
            mz_i++;

            //XML
            len = division->xml->end_positions[xml_i] - division->xml->start_positions[xml_i];
            memcpy(buff+out_len, input_map+division->xml->start_positions[xml_i], len);
            out_len += len;
            xml_i++;

            //inten
            len = division->inten->end_positions[inten_i] - division->inten->start_positions[inten_i];
            memcpy(buff+out_len, input_map+division->inten->start_positions[inten_i], len);
            out_len += len;
            inten_i++;

            j++;
        }
        
        //end case
        len = division->xml->end_positions[xml_i] - division->xml->start_positions[xml_i];
        memcpy(buff+out_len, input_map+division->xml->start_positions[xml_i], len);
        out_len += len;
        xml_i++;

        write_to_file(output_fd, buff, out_len);
    }
    return;
}