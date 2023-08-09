cmake_minimum_required(VERSION 3.15)

project (mscompress)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to choose between library and executable
option(BUILD_SHARED_LIB "Build shared library (node-addon)" ON)

include_directories(
  ${CMAKE_JS_INC}
  vendor/zstd/lib
  vendor/base64/include
  vendor/base64/lib
  vendor/yxml
  src
)

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
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/vendor/config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/vendor/base64/lib/config.h)

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

# Define an option to choose between library and executable
option(BUILD_SHARED_LIB "Build shared library (node-addon)" ON)

# Common source files
file(GLOB COMMON_SOURCE_FILES "src/*.c" "src/*.cpp" "src/*.h")

if (BUILD_SHARED_LIB)
  # Create the shared library (node-addon)
    add_library(${PROJECT_NAME} SHARED ${COMMON_SOURCE_FILES} ${CMAKE_JS_SRC})

    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

    target_link_libraries(${PROJECT_NAME} ${CMAKE_JS_LIB} zstd yxml base64)

    # Include N-API wrappers
    execute_process(COMMAND node -p "require('node-addon-api').include"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE NODE_ADDON_API_DIR
            )
    string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
    string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
    target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
    add_definitions(-DNAPI_VERSION=3)

else()
  # Create the executable
  add_executable(${PROJECT_NAME} ${COMMON_SOURCE_FILES})
  target_include_directories(${PROJECT_NAME} PUBLIC vendor/zstd/lib vendor/base64/include vendor/base64/lib vendor/yxml src)

  if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${ZLIB_LIBRARY} zstd yxml base64)
  else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${ZLIB_LIBRARY} zstd yxml base64 m) # Need math library
  endif()
endif()