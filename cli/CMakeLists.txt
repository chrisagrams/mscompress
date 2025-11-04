cmake_minimum_required(VERSION 3.10)
project(mscompress VERSION 1.0)

# Silence "MSVC is not an assembler for ASM" on newer CMake
if(POLICY CMP0194)
  cmake_policy(SET CMP0194 NEW)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_DEBUG_SYMBOLS "Enable debugging symbols" OFF)

set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build.")

if(APPLE)
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
endif()

if(ENABLE_DEBUG_SYMBOLS)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

file(GLOB SOURCES ${SRC_DIR}/*.c mscompress.c)
add_executable(mscompress ${SOURCES})

# Windows-specific defines
if(WIN32)
  target_compile_definitions(mscompress PRIVATE
    _AMD64_
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
endif()

target_include_directories(mscompress PUBLIC 
  ${VENDOR_DIR}/zstd/lib 
  ${VENDOR_DIR}/base64/include
  ${VENDOR_DIR}/base64/lib
  ${VENDOR_DIR}/yxml 
  ${VENDOR_DIR}/lz4/include
  ${SRC_DIR}
)

# Vendors
include(${VENDOR_DIR}/Lz4Include.cmake)
include(${VENDOR_DIR}/ZlibInclude.cmake)
include(${VENDOR_DIR}/ZstdInclude.cmake)
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)
include(${VENDOR_DIR}/YxmlInclude.cmake)
include(${VENDOR_DIR}/Base64SetEnvFlags.cmake)
include(${VENDOR_DIR}/Base64Include.cmake)

# Link libs (single place)
if(WIN32)
  target_link_libraries(mscompress PRIVATE ${ZLIB_LIBRARY} zstd yxml base64 lz4)
else()
  target_link_libraries(mscompress PRIVATE ${ZLIB_LIBRARY} zstd yxml base64 m lz4)
endif()
