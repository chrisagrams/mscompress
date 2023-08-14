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

# Set directories
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../vendor)

file(GLOB SOURCES ${SRC_DIR}/*.c mscompress.c)
add_executable(mscompress ${SOURCES})

target_include_directories(mscompress PUBLIC ${VENDOR_DIR}/zstd/lib ${VENDOR_DIR}/base64/include ${VENDOR_DIR}/base64/lib ${VENDOR_DIR}/yxml ${SRC_DIR})


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

if(WIN32)
    target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64)
else()
    target_link_libraries(mscompress PRIVATE  ${ZLIB_LIBRARY} zstd yxml base64 m) # need math library
endif()