#include "mscompress.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


/* 
    @section Decoding functions
*/

// void
// algo_decode_lossless (void* args)
// {
//     // Parse args
//     algo_args* a_args = (algo_args*)args;

//     /* Lossless, don't touch anything */

//     //Decode using specified encoding format
//     a_args->dec_fun(a_args->src, )
// }

/*
    @section Encoding functions
*/

void
algo_encode_lossless (void* args)
{
    // Parse args
    algo_args* a_args = (algo_args*)args;
    if(a_args == NULL)
    {
        fprintf(stderr, "algo_encode_cast32: args is NULL\n");
        exit(-1);
    }
    /* Lossless, don't touch anything */

    // Encode using specified encoding format
    a_args->enc_fun(a_args->src, a_args->dest, a_args->dest_len);

    return;
}

void
algo_encode_cast32 (void* args)
{
    // Parse args
    algo_args* a_args = (algo_args*)args;
    if(a_args == NULL)
    {
        fprintf(stderr, "algo_encode_cast32: args is NULL\n");
        exit(-1);
    }

    // Cast 32-bit to 64-bit 
    
    // Get array length
    u_int16_t len = *(uint16_t*)a_args->src;
    if (len <= 0)
    {
        fprintf(stderr, "algo_encode_cast32: len is <= 0\n");
        exit(-1);
    }

    // Get source array 
    float* arr = (float*)((void*)a_args->src + sizeof(u_int16_t));

    // Allocate buffer
    double* res = malloc(sizeof(double) * len);
    if(res == NULL)
    {
        fprintf(stderr, "algo_encode_cast32: malloc failed\n");
        exit(-1);
    }

    // Cast all
    for (size_t i = 0; i < len; i++)
        res[i] = (double)arr[i];

    // Encode using specified encoding format
    a_args->enc_fun(a_args->src, a_args->dest, a_args->dest_len);

    free(res);

    return;
}