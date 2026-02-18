cmake_minimum_required(VERSION 3.15)

# Version is read from .cz.toml (commitizen), the single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/.cz.toml" CZ_TOML)
string(REGEX MATCH "version = \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _ "${CZ_TOML}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Could not parse version from .cz.toml")
endif()
project(mscompress VERSION ${CMAKE_MATCH_1})

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