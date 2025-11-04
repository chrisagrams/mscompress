cmake_minimum_required(VERSION 3.15)

project (mscompress  VERSION 1.0.1)

option(BUILD_CLI "Build command line application" ON)
option(BUILD_NODE "Build node library" ON)
option(BUILD_MODULE "Build python module" ON )

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

# if(BUILD_NODE)
#     add_subdirectory(node)
# endif()

if(BUILD_MODULE)
    add_subdirectory(modules/python)
endif()