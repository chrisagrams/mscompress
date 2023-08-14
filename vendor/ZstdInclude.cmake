file(GLOB ZSTD_SOURCES 
    ${VENDOR_DIR}/zstd/lib/common/*.c
    ${VENDOR_DIR}/zstd/lib/compress/*.c
    ${VENDOR_DIR}/zstd/lib/decompress/*.c
    ${VENDOR_DIR}/zstd/lib/decompress/huf_decompress_amd64.S)

add_library(zstd STATIC ${ZSTD_SOURCES})
enable_language(ASM)
