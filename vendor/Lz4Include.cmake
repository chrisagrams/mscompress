file(GLOB LZ4_RESOURCES 
    ${VENDOR_DIR}/lz4/lib/*.c
)

add_library(lz4 STATIC ${LZ4_RESOURCES})
