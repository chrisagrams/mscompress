cmake_minimum_required(VERSION 3.15)

# Version is read from version.txt (maintained by release-please), the source of truth.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" MSC_VERSION LIMIT_COUNT 1)
string(STRIP "${MSC_VERSION}" MSC_VERSION)
if(NOT MSC_VERSION)
    message(FATAL_ERROR "Could not read version from version.txt")
endif()
project(mscompress VERSION ${MSC_VERSION})

option(BUILD_CLI "Build command line application" ON)
option(BUILD_NODE "Build node library" ON)
option(BUILD_MODULE "Build python module" ON )

if(BUILD_CLI)
    enable_testing()
    add_subdirectory(cli)
endif()

# if(BUILD_NODE)
#     add_subdirectory(node)
# endif()

# if(BUILD_MODULE)
#     add_subdirectory(modules/python)
# endif()