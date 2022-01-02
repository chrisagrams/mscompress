#include "mscompress.h"
#include "vendor/base64/include/libbase64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>


char*
decode_base64(char* src, size_t src_len, size_t* out_len)
/**
 * @brief Wrapper function for base64 libary. 
 * Allocates memory for base64_decode function call and takes care of errors.
 * 
 * @param src Pointer to beginning of base64 string (can be a substring within another string).
 * 
 * @param src_len Length of base64 string (not NULL terminated).
 * 
 * @param out_len Pass-by-reference return value of length of decoded string.
 * 
 * @return Decoded string.
 */
{
    int b64_ret;

    char* b64_out_buff;

    b64_out_buff = (char*)malloc(sizeof(char)*src_len);

    b64_ret = base64_decode(src, src_len, b64_out_buff, out_len, 0);

    if(b64_ret == 0) 
    {
        free(b64_out_buff);
        return NULL;
    }

    return b64_out_buff;
}

int zlib_start_len = BUFSIZE;

double*
decode_zlib(char* src, size_t src_len, size_t* out_len)
/**
 * @brief Wrapper function for zlib library.
 * To optimize for speed, this function dynamically adjusts the buffer size based on 
 * the return value of the uncompress() function. If the buffer was too small for the zlib
 * library, grow the buffer by the increment factor and retry. Since the decoded lengths between
 * spectra are relatively similar within a .mzML file, this function will only hit a few "misses" 
 * in the beginning of the .mzML parsing and stabilize as it progresses. Thus, this solution
 * has a very minimal performance penalty while being memory efficient and scalable. The buffer
 * size is adjusted in the global variable zlib_start_len. One alternative solution is to read
 * all encodedLength accessions within the mzML file but this is a slow procedure do to the 
 * string to integer conversion required. Another solution is to set a fixed buffer size but 
 * this will be a waste of memory on smaller files. 
 * 
 * @param src zlib encoded string.
 * 
 * @param src_len Length of zlib encoded string (not NULL terminated).
 * 
 * @param out_len A pass-by-reference return value of the resulting decompressed string length.
 * 
 * @return 
 */
{
    int zlib_ret = 1;
    int increment = 2;

    *out_len = zlib_start_len;

    Bytef* out_buff;

    while (zlib_ret != Z_OK)
    {
        size_t z_out_len = *out_len;

        out_buff = (Bytef*)malloc(sizeof(Bytef)*z_out_len);
        
        zlib_ret = uncompress(out_buff, &z_out_len, src, src_len);

        switch (zlib_ret)
        {
        case Z_OK:
            *out_len = z_out_len;
            break;
        case Z_BUF_ERROR:
            zlib_start_len *= increment;
            *out_len = zlib_start_len;
            free(out_buff);
            break;
        case Z_STREAM_ERROR:
            zlib_start_len *= increment;
            *out_len = zlib_start_len;
            free(out_buff);
            break;
        case Z_DATA_ERROR:
            free(out_buff);
            return NULL;
        default:
            break;
        }
    }
    
    return out_buff;
    
}

double*
decode_binary(char* input_map, int start_position, int end_position, int compression_method, size_t* out_len)
/**
 * @brief A routine to decode a binary data element within a .mzML file.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @param start_position The start position of base64 data within a <binary></binary> element within the .mzML document.
 * 
 * @param end_position The end position of base64 data within a <binary></binary> element within the .mzML document.
 * 
 * @param compression_method Given by data_format_t struct, the encoding of the binary data within the .mzML document given by preprocess.c:pattern_detect()
 * 
 * @param out_len A pass-by-reference return value of the length of the resulting decoded binary data.
 * 
 * @return
 * 
 */
{
    int zlib_ret;

    size_t zlib_out_len = 0;
    size_t b64_out_len = 0;

    char* b64_out_buff;
    double* zlib_out_buff;

    size_t len = end_position-start_position;

    b64_out_buff = decode_base64(input_map+start_position, len, &b64_out_len);
    
    if (!b64_out_buff)
        return NULL;

    if(compression_method == _zlib_)
    {
        zlib_out_buff = decode_zlib(b64_out_buff, b64_out_len, &zlib_out_len);
        
        if(!zlib_out_buff)
            return NULL;
        
        *out_len = zlib_out_len;
        return zlib_out_buff;
    }

    *out_len = b64_out_len;
    return (double*)b64_out_buff;

}