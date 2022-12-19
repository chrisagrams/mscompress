#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/* 
    @section Decoding functions
*/

void
algo_decode_lossless (void* args)
/**
 * @brief Lossless decoding function.
 * 
 * @param args Pointer to algo_args struct.
 * 
 * @return void
 */
{
    // Parse args
    algo_args* a_args = (algo_args*)args;

    //Decode using specified encoding format
    a_args->dec_fun(*a_args->src, a_args->src_len, a_args->dest, a_args->dest_len);

    /* Lossless, don't touch anything */

    return;
}

void
algo_decode_cast32 (void* args)
{
    // Parse args
    algo_args* a_args = (algo_args*)args;

    char* decoded = NULL;
    size_t decoded_len = 0;

    // Decode using specified encoding format
    a_args->dec_fun(*a_args->src, a_args->src_len, &decoded, &decoded_len);

    // Deternmine length of data based on data format
    uint16_t len;
    float* res;
    
    switch (a_args->src_format)
    {
        case _32f_: {
            len = decoded_len/ sizeof(float);
            res = malloc((len+1) * sizeof(float)); // Allocate space for result and leave room for header
            if(res == NULL)
                error("algo_decode_cast32: malloc failed");
            float* f = (float*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            for(int i = 1; i < len+1; i++)
            {
                res[i] = f[i-1];
            }
            break;
        }
        case _64d_: {
            len = decoded_len/ sizeof(double);
            res = malloc((len+1) * sizeof(float)); // Allocate space for result and leave room for header
            if(res == NULL)
                error("algo_decode_cast32: malloc failed");
            float* f = (float*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            for(int i = 1; i < len+1; i++)
            {
                res[i] = f[i-1];
            }
            break;
        }
        default : {
            free(res);
            error("algo_decode_log_2_transform: Unknown data format");
        }
    }

    // Store length of array in first 4 bytes
    res[0] = (float)len;

    // Return result
    *a_args->dest = res;
    *a_args->dest_len = (len+1) * sizeof(float);

    return;
}

void
algo_decode_log_2_transform (void* args)
/**
 * @brief Log2 transform decoding function.
 * 
 * @param args Pointer to algo_args struct.
 * 
 * @return void
 */
{
    // Parse args
    algo_args* a_args = (algo_args*)args;

    char* decoded = NULL;
    size_t decoded_len = 0;

    //Decode using specified encoding format
    a_args->dec_fun(*a_args->src, a_args->src_len, &decoded, &decoded_len);

    // Deternmine length of data based on data format
    uint16_t len;
    uint16_t* res;

    switch(a_args->src_format)
    {
        case _32f_ : { // 32-bit float
            len = decoded_len / sizeof(float);

            // Perform log2 transform
            res = malloc((len+1) * sizeof(uint16_t)); // Allocate space for result and leave room for header
            if(res == NULL)
                error("algo_decode_log_2_transform: malloc failed");

            double ltran;

            float* f = (float*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            uint16_t* tmp = (uint16_t*)(res) + 1; // Ignore header in first 4 bytes
            
            for(int i = 0; i < len; i++)
            {
                ltran = log2(f[i]);
                tmp[i] = floor(ltran * 100);
            }
            break;
        }
        case _64d_ : { // 64-bit double
            len = decoded_len / sizeof(double);

            // Perform log2 transform
            res = malloc((len+1) * sizeof(uint16_t)); // Allocate space for result and leave room for header
            if(res == NULL)
                error("algo_decode_log_2_transform: malloc failed");

            double ltran;

            double* f = (double*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            uint16_t* tmp = (uint16_t*)(res) + 1; // Ignore header in first 4 bytes
            
            for(int i = 0; i < len; i++)
            {
                ltran = log2(f[i]);
                tmp[i] = floor(ltran * 100);
            }
            break;
        }
        default : {
            free(res);
            error("algo_decode_log_2_transform: Unknown data format");
        }
    }

    // Free decoded buffer
    free(decoded);

    // Store length of array in first 4 bytes
    res[0] = len;

    // Return result
    *a_args->dest = res;
    *a_args->dest_len = (len+1) * sizeof(uint16_t);

    return;
}

/*
    @section Encoding functions
*/

void
algo_encode_lossless (void* args)
/**
 * @brief Lossless encoding function.
 * 
 * @param args Pointer to algo_args struct.
 * 
 * @return void
 */
{
    // Parse args
    algo_args* a_args = (algo_args*)args;
    if(a_args == NULL)
        error("algo_encode_lossless: args is NULL");

    /* Lossless, don't touch anything */

    // Encode using specified encoding format
    a_args->enc_fun(a_args->src, a_args->src_len, a_args->dest, a_args->dest_len);

    return;
}

void
algo_encode_cast32 (void* args)
/**
 * @brief Casts 32-bit float array to 64-bit double array.
 * 
 * @param args Pointer to algo_args struct.
 * 
 * @return void
 */
{
    // Parse args
    algo_args* a_args = (algo_args*)args;
    if(a_args == NULL)
        error("algo_encode_cast32: args is NULL");

    // Cast 32-bit to 64-bit 
    

    // Get source array 
    float* arr = *a_args->src;
    
    // Get array length
    u_int16_t len = (uint16_t)arr[0];

    if (len <= 0)
        error("algo_encode_cast32: len is <= 0");

    // Allocate buffer
    double* res = malloc(sizeof(double) * len);
    if(res == NULL)
        error("algo_encode_cast32: malloc failed");

    // Cast all
    for (size_t i = 1; i < len+1; i++)
        res[i-1] = (double)arr[i];

    // Encode using specified encoding format
    a_args->enc_fun(a_args->src, len*sizeof(uint16_t), a_args->dest, a_args->dest_len);

    free(res);

    return;
}

void
algo_encode_log_2_transform (void* args)
{
    // Parse args
    algo_args* a_args = (algo_args*)args;
    if(a_args == NULL)
        error("algo_encode_log_2_transform: args is NULL");

    // Get array length
    u_int16_t len = *(uint16_t*)(*a_args->src);
    if (len <= 0)
        error("algo_encode_log_2_transform: len is <= 0");

    // Get source array 
    uint16_t* arr = (uint16_t*)((void*)(*a_args->src) + sizeof(u_int16_t));

    switch(a_args->src_format)
    {
        case _32f_ : { // 32-bit float
            // Allocate buffer
            size_t res_len = len * sizeof(float);
            float* res = malloc(res_len);
            if(res == NULL)
                error("algo_encode_log_2_transform: malloc failed");

            // Perform log2 transform
            for (size_t i = 0; i < len; i++)
                res[i] = (float)exp2((double)arr[i] / 100);

            // Encode using specified encoding format
            a_args->enc_fun((char**)(&res), res_len, a_args->dest, a_args->dest_len);
            break;
        }
        case _64d_ : { // 64-bit double
            // Allocate buffer
            double* res = malloc(sizeof(double) * len);
            if(res == NULL)
                error("algo_encode_log_2_transform: malloc failed");

            // Perform log2 transform
            for (size_t i = 0; i < len; i++)
                res[i] = (double)exp2((double)arr[i] / 100);

            // Encode using specified encoding format
            a_args->enc_fun((char**)(&res), len*sizeof(double), a_args->dest, a_args->dest_len);
            break;
        }
        default : {
            error("algo_encode_log_2_transform: Unknown data format");
        }
    }

    return;
}

Algo_ptr
set_compress_algo(char* arg)
/**
 * @brief Sets compression algorithm based on input string.
 * 
 * @param arg String containing compression algorithm name.
 * 
 * @return Algo_ptr Pointer to compression algorithm function.
 */
{
    if(arg == NULL)
        error("set_compress_algo: arg is NULL");
    if(strcmp(arg, "lossless") == 0 || *arg == "" || *arg == '\0')
        return algo_decode_lossless;
    else if(strcmp(arg, "log") == 0)
        return algo_decode_log_2_transform;
    else if(strcmp(arg, "cast") == 0)
        return algo_decode_cast32;
    else
        error("set_compress_algo: Unknown compression algorithm");
}

Algo_ptr
set_decompress_algo(char* arg)
/**
 * @brief Sets decompression algorithm based on input string.
 * 
 * @param arg String containing decompression algorithm name.
 * 
 * @return Algo_ptr Pointer to decompression algorithm function.
 */
{
    if(arg == NULL)
        error("set_decompress_algo: arg is NULL");
    if(strcmp(arg, "lossless") == 0 || *arg == '\0' || *arg == "")
        return algo_encode_lossless;
    else if(strcmp(arg, "log") == 0)
        return algo_encode_log_2_transform;
    else if(strcmp(arg, "cast") == 0)
        return algo_encode_cast32;
    else
        error("set_decompress_algo: Unknown compression algorithm");        
}