#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include "mscompress.h"

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
    a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, a_args->dest, a_args->dest_len, a_args->tmp);

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
    a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded, &decoded_len, a_args->tmp);

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
            error("algo_decode_cast32: Unknown data format");
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
algo_decode_log_2_transform_32f (void* args)
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
    a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded, &decoded_len, a_args->tmp);

    // Deternmine length of data based on data format
    uint16_t len;
    uint16_t* res;

    #ifdef ERROR_CHECK
        if (a_args->src_format != _32f_) // non-essential check, but useful for debugging
            error("algo_decode_log_2_transform_32f: Unknown data format");
    #endif

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

    // Free decoded buffer
    free(decoded);

    // Store length of array in first 4 bytes
    res[0] = len;

    // Return result
    *a_args->dest = res;
    *a_args->dest_len = (len + 1) * sizeof(uint16_t);

    return;
}

void
algo_decode_log_2_transform_64d (void* args)
{
       // Parse args
    algo_args* a_args = (algo_args*)args;

    char* decoded = NULL;
    size_t decoded_len = 0;

    //Decode using specified encoding format
    a_args->dec_fun(a_args->z, *a_args->src, a_args->src_len, &decoded, &decoded_len, a_args->tmp);

    // Deternmine length of data based on data format
    uint16_t len;
    uint16_t* res;

    #ifdef ERROR_CHECK
        if(a_args->src_format != _64d_) // non-essential check, but useful for debugging
            error("algo_decode_log_2_transform_64d: Unknown data format");
    #endif

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
    a_args->enc_fun(a_args->z, a_args->src, a_args->src_len, a_args->dest, a_args->dest_len);

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
    float* arr = (float*)(*a_args->src);
    
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
            a_args->enc_fun(a_args->z, &ptr, len * sizeof(float), a_args->dest, a_args->dest_len);

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
            a_args->enc_fun(a_args->z, &res, len*sizeof(double), a_args->dest, a_args->dest_len);

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
algo_encode_log_2_transform_32f (void* args)
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

    #ifdef ERROR_CHECK
        if (a_args->src_format != _32f_) // non-essential check, but useful for debugging
            error("algo_encode_log_2_transform: Unknown data format");
    #endif

    // Allocate buffer
    size_t res_len = len * sizeof(float);
    float* res = malloc(res_len);
    
    #ifdef ERROR_CHECK
        if(res == NULL)
            error("algo_encode_log_2_transform: malloc failed");
    #endif

    // Perform log2 transform
    for (size_t i = 0; i < len; i++)
        res[i] = (float)exp2((double)arr[i] / 100);

    // Encode using specified encoding format
    a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest, a_args->dest_len);

    // Move to next array
    *a_args->src += (len * sizeof(uint16_t)) + ZLIB_SIZE_OFFSET;    

    return;
}

void
algo_encode_log_2_transform_64d (void* args)
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

    #ifdef ERROR_CHECK
        if (a_args->src_format != _64d_) // non-essential check, but useful for debugging
            error("algo_encode_log_2_transform: Unknown data format");
    #endif

   // Allocate buffer
    size_t res_len = len * sizeof(double);
    double* res = malloc(res_len);
    if(res == NULL)
        error("algo_encode_log_2_transform: malloc failed");
    // Perform log2 transform
    for (size_t i = 0; i < len; i++)
        res[i] = (double)exp2((double)arr[i] / 100);
        
    // Encode using specified encoding format
    a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest, a_args->dest_len);

    // Move to next array
    *a_args->src += (len*sizeof(uint16_t)) + ZLIB_SIZE_OFFSET;

    // Encode using specified encoding format
    a_args->enc_fun(a_args->z, (char**)(&res), res_len, a_args->dest, a_args->dest_len);

    // Move to next array
    *a_args->src += (len * sizeof(uint16_t)) + ZLIB_SIZE_OFFSET;

    return;
}

Algo_ptr
set_compress_algo(int algo, int accession)
{
    switch(algo)
    {
        case _lossless_ :       return algo_decode_lossless;
        case _log2_transform_ :
        {
            switch(accession)
            {
                case _32f_ :    return algo_decode_log_2_transform_32f;
                case _64d_ :    return algo_decode_log_2_transform_64d;
            }
        } ;
        case _cast_64_to_32_:   return algo_decode_cast32;
        default:                error("set_compress_algo: Unknown compression algorithm");
    }
}

Algo_ptr
set_decompress_algo(int algo, int accession)
{   
    switch(algo)
    {
        case _lossless_ :       return algo_encode_lossless;
        case _log2_transform_ : 
        {
            switch(accession)
            {
                case _32f_ :    return algo_encode_log_2_transform_32f;
                case _64d_ :    return algo_encode_log_2_transform_64d;
            }
        } ;
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