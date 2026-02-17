cmake_minimum_required(VERSION 3.10)

# Version is read from .cz.toml (commitizen), the single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../.cz.toml" CZ_TOML)
string(REGEX MATCH "version = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${CZ_TOML}")
project(mscompress VERSION ${CMAKE_MATCH_1})

# Set the build type to Release by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Add an option to enable/disable debugging symbols
option(ENABLE_DEBUG_SYMBOLS "Enable debugging symbols" OFF)

# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)

option(CODE_COVERAGE "Enable coverage reporting" OFF)

# Set the build type based on the CMAKE_BUILD_TYPE variable
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build.")

if(APPLE)
    # Set the compiler to Clang
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
endif()

# Set the build type to Debug if debugging symbols are enabled
if(ENABLE_DEBUG_SYMBOLS)
    set(CMAKE_BUILD_TYPE Debug)
else()
    # Silence warnings for non-debug builds
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # add_compile_options("-w")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        # add_compile_options("/W0")
    endif()
endif()

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

file(GLOB SOURCES ${SRC_DIR}/*.c mscompress.c)
add_executable(mscompress ${SOURCES})
target_compile_definitions(mscompress PRIVATE VERSION="${PROJECT_VERSION}")

# Ensure winnt.h sees a target arch and keep headers tidy on MSVC
if(WIN32)
  target_compile_definitions(mscompress PRIVATE
    _AMD64_
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
  )
else()
    # Find Threads package for non-Windows systems
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)

endif()

target_include_directories(mscompress PUBLIC 
                            ${VENDOR_DIR}/zstd/lib 
                            ${VENDOR_DIR}/base64/include
                            ${VENDOR_DIR}/base64/lib
                            ${VENDOR_DIR}/yxml 
                            ${VENDOR_DIR}/lz4/include
                            ${SRC_DIR})

#lz4
include (${VENDOR_DIR}/Lz4Include.cmake) # Add lz4 library

#zlib
include (${VENDOR_DIR}/ZlibInclude.cmake) # Add zlib library

#zstd
include (${VENDOR_DIR}/ZstdInclude.cmake) # Add zstd library
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C) # set zstd library language to C

#yxml
include (${VENDOR_DIR}/YxmlInclude.cmake) # Add yxml library

#base64
include (${VENDOR_DIR}/Base64SetEnvFlags.cmake) # set compiler flags
include (${VENDOR_DIR}/Base64Include.cmake) # Add base64 library

# Link libs - only once, after all libraries are defined
if(WIN32)
    target_link_libraries(mscompress PRIVATE base64 lz4 ${ZLIB_LIBRARY} zstd yxml)
else()
    target_link_libraries(mscompress PRIVATE base64 lz4 ${ZLIB_LIBRARY} zstd yxml m Threads::Threads) # need math library and pthreads on Linux/Mac
endif()

if(CODE_COVERAGE)
    message(STATUS "Applying coverage flags to mscompress target")
    target_compile_options(mscompress PRIVATE --coverage -O0 -g)
    target_link_options(mscompress PRIVATE --coverage)
endif()


# Enable testing
enable_testing()

# Add the test subdirectory
add_subdirectory(test)
