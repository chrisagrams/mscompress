#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <float.h>

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

    #ifdef ERROR_CHECK
        if(a_args->src == NULL)
            error("algo_decode_lossless: src is NULL");
    #endif

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
        case _32f_: 
        {
            len = decoded_len / sizeof(float);
            res = malloc((len + 1) * sizeof(float)); // Allocate space for result and leave room for header

            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_decode_cast32: malloc failed");
            #endif

            float* f = (float*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            for(int i = 1; i < len + 1; i++)
            {
                res[i] = f[i-1];
            }
            break;
        }
        case _64d_: 
        {
            len = decoded_len / sizeof(double);
            res = malloc((len + 1) * sizeof(float)); // Allocate space for result and leave room for header
            
            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_decode_cast32: malloc failed");
            #endif

            double* f = (double*)(decoded + ZLIB_SIZE_OFFSET); // Ignore header in first 4 bytes
            for(int i = 1; i < len + 1; i++)
            {
                res[i] = (float)f[i-1];
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
    *a_args->dest_len = (len +1 ) * sizeof(float);

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
            res = malloc((len + 1) * sizeof(uint16_t)); // Allocate space for result and leave room for header

            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_decode_log_2_transform: malloc failed");
            #endif

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
            res = malloc((len + 1) * sizeof(uint16_t)); // Allocate space for result and leave room for header

            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_decode_log_2_transform: malloc failed");
            #endif

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
    *a_args->dest_len = (len + 1) * sizeof(uint16_t);

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
    
    #ifdef ERROR_CHECK
        if(a_args == NULL)
            error("algo_encode_lossless: args is NULL");
    #endif

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

    #ifdef ERROR_CHECK
        if(a_args == NULL)
            error("algo_encode_cast32: args is NULL");
    #endif

    // Cast 32-bit to 64-bit 
    
    // Get source array 
    float* arr = *a_args->src;
    
    // Get array length
    u_int16_t len = (uint16_t)arr[0];


    #ifdef ERROR_CHECK
        if (len <= 0)
            error("algo_encode_cast32: len is <= 0");
    #endif

    switch(a_args->src_format)
    {
        case _32f_ : // 32-bit float (no need to cast)
        {
            // Move dereferenced src pointer to skip header
            char* ptr = *a_args->src + sizeof(float);

            // Encode using specified encoding format
            a_args->enc_fun(&ptr, len * sizeof(float), a_args->dest, a_args->dest_len);

            // Move src pointer
            *a_args->src += (len + 1) * sizeof(float);
            break;
        }
        case _64d_ :  // 64-bit double
        {
            // Allocate buffer
            void* res = malloc(sizeof(double) * len);

            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_encode_cast32: malloc failed");
            #endif

            double* res_arr = (double*)res;

            // Cast all
            for (size_t i = 1; i < len + 1; i++)
                res_arr[i-1] = (double)arr[i];
            
            // Encode using specified encoding format
            a_args->enc_fun(&res, len*sizeof(double), a_args->dest, a_args->dest_len);

            // Move src pointer
            *a_args->src += (len+1)*sizeof(float);

            // // Free buffer
            // free(res);
            break;
        }
    }
    return;
}

void
algo_encode_log_2_transform (void* args)
{
    // Parse args
    algo_args* a_args = (algo_args*)args;

    #ifdef ERROR_CHECK
        if(a_args == NULL)
            error("algo_encode_log_2_transform: args is NULL");
    #endif

    // Get array length
    u_int16_t len = *(uint16_t*)(*a_args->src);

    #ifdef ERROR_CHECK
        if (len <= 0)
            error("algo_encode_log_2_transform: len is <= 0");
    #endif

    // Get source array 
    uint16_t* arr = (uint16_t*)((void*)(*a_args->src) + sizeof(u_int16_t));

    switch(a_args->src_format)
    {
        case _32f_ :  // 32-bit float
        {
            // Allocate buffer
            size_t res_len = len * sizeof(float);
            float* res = malloc(res_len);
            
            #ifdef ERROR_CHECK
                if(res == NULL)
                    error("algo_encode_log_2_transform: malloc failed");
            #endif

            // Perform log2 transform
            for (size_t i = 0; i < len; i++)
            {
                if(arr[i] == 0)
                    res[i] = 0;
                // if(isnormal((double)arr[i]/100) == 0)
                //     error("algo_encode_log_2_transform: arr[i]/100 is not normal");
                else
                    res[i] = (float)exp2((double)arr[i] / 100);
                if(isinf(res[i]))
                    res[i] = 0;
                if(isnormal(res[i]) == 0 && res[i] != 0)
                    error("algo_encode_log_2_transform: res[i] is not normal");
            }

            // Encode using specified encoding format
            a_args->enc_fun((char**)(&res), res_len, a_args->dest, a_args->dest_len);

            // Move to next array
            *a_args->src += (len * sizeof(uint16_t)) + ZLIB_SIZE_OFFSET;
            break;
        }
        case _64d_ :  // 64-bit double
        {
            // Allocate buffer
            size_t res_len = len * sizeof(double);
            double* res = malloc(res_len);
            if(res == NULL)
                error("algo_encode_log_2_transform: malloc failed");
            // Perform log2 transform
            for (size_t i = 0; i < len; i++)
                res[i] = (double)exp2((double)arr[i] / 100);
                
            // Encode using specified encoding format
            a_args->enc_fun((char**)(&res), res_len, a_args->dest, a_args->dest_len);

            // Move to next array
            *a_args->src += (len*sizeof(uint16_t)) + ZLIB_SIZE_OFFSET;
            break;
        }
    }

    return;
}

Algo_ptr
set_compress_algo(int algo)
{
    switch(algo)
    {
        case _lossless_ :       return algo_decode_lossless;
        case _log2_transform_ : return algo_decode_log_2_transform;
        case _cast_64_to_32_:   return algo_decode_cast32;
        default:                error("set_compress_algo: Unknown compression algorithm");
    }
}

Algo_ptr
set_decompress_algo(int algo)
{   
    switch(algo)
    {
        case _lossless_ :       return algo_encode_lossless;
        case _log2_transform_ : return algo_encode_log_2_transform;
        case _cast_64_to_32_:   return algo_encode_cast32;
        default:                error("set_decompress_algo: Unknown compression algorithm");
    }
}

int
get_algo_type(char* arg)
{
    if(arg == NULL)
        error("get_algo_type: arg is NULL");
    if(strcmp(arg, "lossless") == 0 || *arg == '\0' || *arg == "")
        return _lossless_;
    else if(strcmp(arg, "log") == 0)
        return _log2_transform_;
    else if(strcmp(arg, "cast") == 0)
        return _cast_64_to_32_;
    else
        error("get_algo_type: Unknown compression algorithm");
}