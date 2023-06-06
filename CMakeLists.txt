cmake_minimum_required(VERSION 3.10)
project(mscompress VERSION 1.0)

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



file(GLOB SOURCES src/*.c)
add_executable(mscompress ${SOURCES})
target_include_directories(mscompress PUBLIC vendor/zstd/lib vendor/base64/include vendor/base64/lib vendor/yxml src)

set(ZLIB_SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/zlib)
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

# Set the variables based on environment flags
if(DEFINED ENV{AVX2_CFLAGS})
  set(HAVE_AVX2 1)
else()
  set(HAVE_AVX2 0)
endif()

if(DEFINED ENV{NEON32_CFLAGS})
  set(HAVE_NEON32 1)
else()
  set(HAVE_NEON32 0)
endif()

if(DEFINED ENV{NEON64_CFLAGS})
  set(HAVE_NEON64 1)
else()
  set(HAVE_NEON64 0)
endif()

if(DEFINED ENV{SSSE3_CFLAGS})
  set(HAVE_SSSE3 1)
else()
  set(HAVE_SSSE3 0)
endif()

if(DEFINED ENV{SSE41_CFLAGS})
  set(HAVE_SSE41 1)
else()
  set(HAVE_SSE41 0)
endif()

if(DEFINED ENV{SSE42_CFLAGS})
  set(HAVE_SSE42 1)
else()
  set(HAVE_SSE42 0)
endif()

if(DEFINED ENV{AVX_CFLAGS})
  set(HAVE_AVX 1)
else()
  set(HAVE_AVX 0)
endif()

# Generate the config.h file
configure_file(vendor/config.h.in vendor/base64/lib/config.h)

if(WIN32)
  target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64)
else()
  target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64 m) #need math library
endif()