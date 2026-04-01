#ifndef ALGOS_H
#define ALGOS_H

/* lossless.c */
void algo_decode_lossless(void* args);
void algo_encode_lossless(void* args);

/* cast.c */
void algo_decode_cast32_64d(void* args);
void algo_decode_cast16_32f(void* args);
void algo_decode_cast16_64d(void* args);
void algo_encode_cast32_64d(void* args);
void algo_encode_cast16_32f(void* args);
void algo_encode_cast16_64d(void* args);

/* log2.c */
void algo_decode_log_2_transform_32f(void* args);
void algo_decode_log_2_transform_64d(void* args);
void algo_encode_log_2_transform_32f(void* args);
void algo_encode_log_2_transform_64d(void* args);

/* delta.c */
void algo_decode_delta16_transform_32f(void* args);
void algo_decode_delta16_transform_64d(void* args);
void algo_decode_delta24_transform_32f(void* args);
void algo_decode_delta24_transform_64d(void* args);
void algo_decode_delta32_transform_32f(void* args);
void algo_decode_delta32_transform_64d(void* args);
void algo_encode_delta16_transform_32f(void* args);
void algo_encode_delta16_transform_64d(void* args);
void algo_encode_delta24_transform_32f(void* args);
void algo_encode_delta24_transform_64d(void* args);
void algo_encode_delta32_transform_32f(void* args);
void algo_encode_delta32_transform_64d(void* args);

/* vdelta.c */
void algo_decode_vdelta16_transform_32f(void* args);
void algo_decode_vdelta16_transform_64d(void* args);
void algo_decode_vdelta24_transform_32f(void* args);
void algo_decode_vdelta24_transform_64d(void* args);
void algo_encode_vdelta16_transform_32f(void* args);
void algo_encode_vdelta16_transform_64d(void* args);
void algo_encode_vdelta24_transform_32f(void* args);
void algo_encode_vdelta24_transform_64d(void* args);

/* vbr.c */
void algo_decode_vbr_32f(void* args);
void algo_decode_vbr_64d(void* args);
void algo_encode_vbr_32f(void* args);
void algo_encode_vbr_64d(void* args);

/* cast24.c */
void algo_decode_cast24_32f(void* args);
void algo_decode_cast24_64d(void* args);
void algo_encode_cast24_32f(void* args);
void algo_encode_cast24_64d(void* args);

/* topn.c */
void algo_decode_topn_32f(void* args);
void algo_decode_topn_64d(void* args);

/* bitpack.c */
void algo_decode_bitpack_32f(void* args);
void algo_decode_bitpack_64d(void* args);
void algo_encode_bitpack_32f(void* args);
void algo_encode_bitpack_64d(void* args);

#endif /* ALGOS_H */
