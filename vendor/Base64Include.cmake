set(BASE64_SOURCES
    ${VENDOR_DIR}/base64/lib/arch/avx2/codec.c
    ${VENDOR_DIR}/base64/lib/arch/generic/codec.c
    ${VENDOR_DIR}/base64/lib/arch/neon32/codec.c
    ${VENDOR_DIR}/base64/lib/arch/neon64/codec.c
    ${VENDOR_DIR}/base64/lib/arch/ssse3/codec.c
    ${VENDOR_DIR}/base64/lib/arch/sse41/codec.c
    ${VENDOR_DIR}/base64/lib/arch/sse42/codec.c
    ${VENDOR_DIR}/base64/lib/arch/avx/codec.c
    ${VENDOR_DIR}/base64/lib/lib.c
    ${VENDOR_DIR}/base64/lib/codec_choose.c
    ${VENDOR_DIR}/base64/lib/tables/tables.c
)

add_library(base64 STATIC ${BASE64_SOURCES})

target_include_directories(base64 PUBLIC lib)