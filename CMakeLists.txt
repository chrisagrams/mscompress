cmake_minimum_required(VERSION 3.10)
project(mscompress VERSION 1.0)
file(GLOB SOURCES src/*.c)
add_executable(mscompress ${SOURCES})
target_include_directories(mscompress PUBLIC vendor/zstd/lib vendor/zlib vendor/base64/include vendor/base64/lib vendor/yxml src)
add_subdirectory(vendor/zlib) # zlib library



#zstd
file(GLOB ZSTD_SOURCES 
    vendor/zstd/lib/common/*.c
    vendor/zstd/lib/compress/*.c
    vendor/zstd/lib/decompress/*.c
    vendor/zstd/lib/decompress/huf_decompress_amd64.S)

add_library(zstd STATIC ${ZSTD_SOURCES})
enable_language(ASM)
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)


#yxml
file(GLOB YXML_SOURCES vendor/yxml/*.c)
add_library(yxml STATIC ${YXML_SOURCES})
set_target_properties(yxml PROPERTIES LINKER_LANGUAGE C)

#base64

set(BASE64_SOURCES
  vendor/base64/lib/arch/avx2/codec.c
  vendor/base64/lib/arch/generic/codec.c
  vendor/base64/lib/arch/neon32/codec.c
  vendor/base64/lib/arch/neon64/codec.c
  vendor/base64/lib/arch/ssse3/codec.c
  vendor/base64/lib/arch/sse41/codec.c
  vendor/base64/lib/arch/sse42/codec.c
  vendor/base64/lib/arch/avx/codec.c
  vendor/base64/lib/lib.c
  vendor/base64/lib/codec_choose.c
  vendor/base64/lib/tables/tables.c
)

add_library(base64 STATIC ${BASE64_SOURCES})

target_include_directories(base64 PUBLIC lib)

if($ENV{AVX2_CFLAGS})
  set_source_files_properties(lib/arch/avx2/codec.c PROPERTIES COMPILE_FLAGS ${AVX2_CFLAGS})
endif()

if($ENV{NEON32_CFLAGS})
  set_source_files_properties(lib/arch/neon32/codec.c PROPERTIES COMPILE_FLAGS ${NEON32_CFLAGS})
endif()

if($ENV{NEON64_CFLAGS})
  set_source_files_properties(lib/arch/neon64/codec.c PROPERTIES COMPILE_FLAGS ${NEON64_CFLAGS})
endif()

if($ENV{SSSE3_CFLAGS})
  set_source_files_properties(lib/arch/ssse3/codec.c PROPERTIES COMPILE_FLAGS ${SSSE3_CFLAGS})
endif()

if($ENV{SSE41_CFLAGS})
  set_source_files_properties(lib/arch/sse41/codec.c PROPERTIES COMPILE_FLAGS ${SSE41_CFLAGS})
endif()

if($ENV{SSE42_CFLAGS})
  set_source_files_properties(lib/arch/sse42/codec.c PROPERTIES COMPILE_FLAGS ${SSE42_CFLAGS})
endif()

if($ENV{AVX_CFLAGS})
  set_source_files_properties(lib/arch/avx/codec.c PROPERTIES COMPILE_FLAGS ${AVX_CFLAGS})
endif()

if($ENV{OPENMP})
  target_compile_options(base64 PUBLIC -fopenmp)
endif()

target_link_libraries(mscompress PRIVATE zlib zstd yxml base64 m)
