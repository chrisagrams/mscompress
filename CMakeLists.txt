cmake_minimum_required(VERSION 3.10)
project(mscompress VERSION 1.0)
file(GLOB SOURCES src/*.c)
add_executable(mscompress ${SOURCES})
target_include_directories(mscompress PUBLIC vendor/zlib vendor/yxml vendor/base64/include src)
add_subdirectory(vendor/zlib) # zlib library


add_library(zstd STATIC
    vendor/zstd/lib/common
    vendor/zstd/lib/compress
    vendor/zstd/lib/decompress
)

set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)

target_include_directories(zstd PUBLIC vendor/zstd/lib)


target_link_libraries(mscompress PRIVATE zlib zstd)
