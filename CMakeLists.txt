cmake_minimum_required(VERSION 3.15)

project (mscompress  VERSION 1.0)

option(BUILD_CLI "Build command line application" ON)
option(BUILD_NODE "Build node library" ON)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_NODE)
    add_subdirectory(node)
endif()