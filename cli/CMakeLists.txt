cmake_minimum_required(VERSION 3.10)
project(mscompress VERSION 1.0)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/build)

# Set the build type to Release by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Add an option to enable/disable debugging symbols
option(ENABLE_DEBUG_SYMBOLS "Enable debugging symbols" OFF)

# Set the build type based on the CMAKE_BUILD_TYPE variable
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build.")

# Set the build type to Debug if debugging symbols are enabled
if(ENABLE_DEBUG_SYMBOLS)
    set(CMAKE_BUILD_TYPE Debug)
else()
    # Silence warnings for non-debug builds
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options("-w")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        add_compile_options("/W0")
    endif()
endif()

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

file(GLOB SOURCES ${SRC_DIR}/*.c mscompress.c)
add_executable(mscompress ${SOURCES})

target_include_directories(mscompress PUBLIC ${VENDOR_DIR}/zstd/lib ${VENDOR_DIR}/base64/include ${VENDOR_DIR}/base64/lib ${VENDOR_DIR}/yxml ${SRC_DIR})

set(ZLIB_SOURCE_DIR ${VENDOR_DIR}/zlib)
# Add a custom target to run nmake and build zlib
if(WIN32)
    add_custom_target(
        zlib_build
        COMMAND nmake -f ${ZLIB_SOURCE_DIR}/win32/Makefile.msc
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
else()
    add_custom_target(
        zlib_build
        COMMAND ${ZLIB_SOURCE_DIR}/configure
        COMMAND make -C ${ZLIB_SOURCE_DIR}
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
endif()
# Set the location of the built zlib library
if(WIN32)
    set(ZLIB_LIBRARY "${ZLIB_SOURCE_DIR}/zlib.lib")
else()
    set(ZLIB_LIBRARY "${ZLIB_SOURCE_DIR}/libz.a")
endif()

include_directories(${ZLIB_SOURCE_DIR})
add_dependencies(mscompress zlib_build)



#zstd
file(GLOB ZSTD_SOURCES 
    ${VENDOR_DIR}/zstd/lib/common/*.c
    ${VENDOR_DIR}/zstd/lib/compress/*.c
    ${VENDOR_DIR}/zstd/lib/decompress/*.c
    ${VENDOR_DIR}/zstd/lib/decompress/huf_decompress_amd64.S)

add_library(zstd STATIC ${ZSTD_SOURCES})
enable_language(ASM)
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)


#yxml
file(GLOB YXML_SOURCES ${VENDOR_DIR}/yxml/*.c)
add_library(yxml STATIC ${YXML_SOURCES})
set_target_properties(yxml PROPERTIES LINKER_LANGUAGE C)

#base64
include (${VENDOR_DIR}/Base64SetEnvFlags.cmake) # set compiler flags

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

if(WIN32)
    target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64)
else()
    target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64 m) # need math library
endif()