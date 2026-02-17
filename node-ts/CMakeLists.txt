cmake_minimum_required(VERSION 3.15)

# Version is read from .cz.toml (commitizen), the single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../.cz.toml" CZ_TOML)
string(REGEX MATCH "version = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${CZ_TOML}")
project(mscompress VERSION ${CMAKE_MATCH_1})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
endif()

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

include_directories(
  ${CMAKE_JS_INC}
  ${VENDOR_DIR}/zlib
  ${VENDOR_DIR}/zstd/lib
  ${VENDOR_DIR}/base64/include
  ${VENDOR_DIR}/base64/lib
  ${VENDOR_DIR}/yxml
  ${VENDOR_DIR}/lz4/include
  ${SRC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/native
)

# C core sources
file(GLOB C_SOURCE_FILES ${SRC_DIR}/*.c)

# C++ native addon sources
set(CPP_SOURCE_FILES
  src/native/addon.cpp
  src/native/types.cpp
  src/native/file_ops.cpp
  src/native/format.cpp
  src/native/division.cpp
  src/native/spectrum.cpp
  src/native/compress.cpp
  src/native/decompress.cpp
  src/native/extract.cpp
)

add_library(${PROJECT_NAME} SHARED ${C_SOURCE_FILES} ${CPP_SOURCE_FILES} ${CMAKE_JS_SRC})
target_compile_definitions(${PROJECT_NAME} PRIVATE VERSION="${PROJECT_VERSION}")

# Windows SDK headers (winnt.h) require an architecture macro to be defined
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _AMD64_=1)
endif()

# Vendor libraries
include(${VENDOR_DIR}/Lz4Include.cmake)
# Build zlib via add_subdirectory instead of ZlibInclude.cmake so Ninja can
# track the output properly (add_custom_target does not declare file outputs).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(${VENDOR_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib_build)
unset(CMAKE_POLICY_VERSION_MINIMUM)
include(${VENDOR_DIR}/ZstdInclude.cmake)
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)
include(${VENDOR_DIR}/YxmlInclude.cmake)
include(${VENDOR_DIR}/Base64SetEnvFlags.cmake)
include(${VENDOR_DIR}/Base64Include.cmake)

# Node addon settings
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

# Linking
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} zstd yxml base64 lz4 zlib)

# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
add_definitions(-DNAPI_VERSION=8)
