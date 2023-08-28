#include <iostream>
#include <vector>
#include <variant>
#include "mscompress.h"

namespace mscompress {

    class MassSpectrum {
        public:
            MassSpectrum() {};
            void add(double mz, double intensity, double retention_time) {
                this->mz.push_back(mz);
                this->intensity.push_back(intensity);
                this->retention_time.push_back(retention_time);
            }

        private: 
            std::vector<double> mz;
            std::vector<double> intensity;
            std::vector<double> retention_time;
    };

    std::variant<std::vector<float>, std::vector<double>> 
    decodeAndDecompress(
        int source_compression,
        int source_format,
        char* src,
        size_t src_len
    )
    {
        size_t out_len = 0;
        size_t max_buffer_size = ((src_len + 3) / 4) * 3;
        char* dest = (char*)malloc(max_buffer_size);

        decode_base64(src, dest, src_len, &out_len);

        switch(source_compression) {
            case _zlib_:
            {
                z_stream* z = alloc_z_stream();
                zlib_block_t* decmp_output = zlib_alloc(0);
                if(decmp_output == NULL) {
                    throw std::runtime_error("Error in zlib_alloc");
                }
                ZLIB_TYPE decmp_size = (ZLIB_TYPE)zlib_decompress(z, (Bytef*)dest, decmp_output, out_len);
                dest = (char*)decmp_output->buff;
                out_len = decmp_size;
                break;
            }
            case _no_comp_:
            {
                char* new_dest = (char*)realloc(dest, out_len);
                if (new_dest == NULL) {
                    free(dest);
                    throw std::runtime_error("Error in realloc");
                }
                dest = new_dest;
                break;
            }
            default:
                throw std::runtime_error("df does not contain proper source_compression");
        }

        switch(source_format) {
            case _64d_:
            {
                std::vector<double> result((double*)dest, (double*)(dest + out_len));
                return result;
            }
            case _32f_:
            {
                std::vector<float> result((float*)dest, (float*)(dest + out_len));
                return result;
            }
            default:
                throw std::runtime_error("df does not contain proper source_fmt");
        }
    }

    MassSpectrum appendAll(data_format_t* df, division_t* div, char* input_map) {
        MassSpectrum ms;

        uint32_t num_spec = df->source_total_spec;

        for (uint32_t i = 0; i < num_spec; i++)
        {
            uint64_t mz_start_position = div->mz->start_positions[i];
            uint64_t mz_end_position = div->mz->end_positions[i];
            char* src = input_map + mz_start_position;
            size_t src_len = mz_end_position - mz_start_position;

            auto mz = decodeAndDecompress(df->source_compression, df->source_mz_fmt, src, src_len);

            //TODO, finish this
        }

        return ms;
    }
}