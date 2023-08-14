cmake_minimum_required(VERSION 3.15)

project (mscompress  VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)


include_directories(
  ${CMAKE_JS_INC}
  ${VENDOR_DIR}/zstd/lib
  ${VENDOR_DIR}/base64/include
  ${VENDOR_DIR}/base64/lib
  ${VENDOR_DIR}/yxml
  ${SRC_DIR}
)

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

# Define an option to choose between library and executable
option(BUILD_SHARED_LIB "Build shared library (node-addon)" ON)

# Common source files
file(GLOB SOURCE_FILES ${SRC_DIR}/*.c wrapper.cpp)


# Create the shared library (node-addon)
add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})

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
