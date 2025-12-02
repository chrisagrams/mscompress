# Base source files (no special flags needed)
set(BASE64_SOURCES_COMMON
    ${VENDOR_DIR}/base64/lib/arch/generic/codec.c
    ${VENDOR_DIR}/base64/lib/lib.c
    ${VENDOR_DIR}/base64/lib/codec_choose.c
    ${VENDOR_DIR}/base64/lib/tables/tables.c
)

# Architecture-specific sources
set(BASE64_SOURCES_SSSE3 ${VENDOR_DIR}/base64/lib/arch/ssse3/codec.c)
set(BASE64_SOURCES_SSE41 ${VENDOR_DIR}/base64/lib/arch/sse41/codec.c)
set(BASE64_SOURCES_SSE42 ${VENDOR_DIR}/base64/lib/arch/sse42/codec.c)
set(BASE64_SOURCES_AVX ${VENDOR_DIR}/base64/lib/arch/avx/codec.c)
set(BASE64_SOURCES_AVX2 ${VENDOR_DIR}/base64/lib/arch/avx2/codec.c)
set(BASE64_SOURCES_AVX512 ${VENDOR_DIR}/base64/lib/arch/avx512/codec.c)
set(BASE64_SOURCES_NEON32 ${VENDOR_DIR}/base64/lib/arch/neon32/codec.c)
set(BASE64_SOURCES_NEON64 ${VENDOR_DIR}/base64/lib/arch/neon64/codec.c)

add_library(base64 STATIC 
    ${BASE64_SOURCES_COMMON}
    ${BASE64_SOURCES_SSSE3}
    ${BASE64_SOURCES_SSE41}
    ${BASE64_SOURCES_SSE42}
    ${BASE64_SOURCES_AVX}
    ${BASE64_SOURCES_AVX2}
    ${BASE64_SOURCES_AVX512}
    ${BASE64_SOURCES_NEON32}
    ${BASE64_SOURCES_NEON64}
)

target_include_directories(base64 PUBLIC 
    ${VENDOR_DIR}/base64/include
    ${VENDOR_DIR}/base64/lib
    ${VENDOR_DIR}/base64/lib/tables
)

# Define BASE64_STATIC_DEFINE to indicate we're using base64 as a static library
# This prevents __declspec(dllimport) on Windows and is harmless on other platforms
target_compile_definitions(base64 PUBLIC BASE64_STATIC_DEFINE)

# Set architecture-specific compiler flags for x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|x86|i686|i386")
    if(MSVC)
        # MSVC flags
        set_source_files_properties(${BASE64_SOURCES_SSSE3} PROPERTIES COMPILE_FLAGS "/arch:SSSE3")
        set_source_files_properties(${BASE64_SOURCES_SSE41} PROPERTIES COMPILE_FLAGS "/arch:SSE4.1")
        set_source_files_properties(${BASE64_SOURCES_SSE42} PROPERTIES COMPILE_FLAGS "/arch:SSE4.2")
        set_source_files_properties(${BASE64_SOURCES_AVX} PROPERTIES COMPILE_FLAGS "/arch:AVX")
        set_source_files_properties(${BASE64_SOURCES_AVX2} PROPERTIES COMPILE_FLAGS "/arch:AVX2")
        set_source_files_properties(${BASE64_SOURCES_AVX512} PROPERTIES COMPILE_FLAGS "/arch:AVX512")
    else()
        # GCC/Clang flags
        set_source_files_properties(${BASE64_SOURCES_SSSE3} PROPERTIES COMPILE_FLAGS "-mssse3")
        set_source_files_properties(${BASE64_SOURCES_SSE41} PROPERTIES COMPILE_FLAGS "-msse4.1")
        set_source_files_properties(${BASE64_SOURCES_SSE42} PROPERTIES COMPILE_FLAGS "-msse4.2")
        set_source_files_properties(${BASE64_SOURCES_AVX} PROPERTIES COMPILE_FLAGS "-mavx")
        set_source_files_properties(${BASE64_SOURCES_AVX2} PROPERTIES COMPILE_FLAGS "-mavx2")
        set_source_files_properties(${BASE64_SOURCES_AVX512} PROPERTIES COMPILE_FLAGS "-mavx512vl -mavx512bw")
    endif()
# Set architecture-specific compiler flags for ARM
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64.*|AARCH64.*|arm64.*|ARM64.*)")
    if(NOT MSVC)
        # ARM64 NEON is enabled by default on aarch64, but be explicit
        set_source_files_properties(${BASE64_SOURCES_NEON64} PROPERTIES COMPILE_FLAGS "")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm.*|ARM.*)")
    if(NOT MSVC)
        set_source_files_properties(${BASE64_SOURCES_NEON32} PROPERTIES COMPILE_FLAGS "-mfpu=neon")
    endif()
endif()
