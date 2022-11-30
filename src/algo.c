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

    // Cast 32-bit to 64-bit 
    
    // Get array length
    u_int16_t len = *(uint16_t*)a_args->src;

    // Get source array 
    float* arr = (float*)((void*)a_args->src + sizeof(u_int16_t));

    // Allocate buffer
    double* res = (double*)malloc(sizeof(double) * len);

    // Cast all
    for (size_t i = 0; i < len; i++)
        res[i] = (double)arr[i];

    // Encode using specified encoding format
    a_args->enc_fun(a_args->src, a_args->dest, a_args->dest_len);

    free(res);

    return;

}