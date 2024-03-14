cmake_minimum_required(VERSION 3.15)

project (mscompress  VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Set the compiler to Clang
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
  ${SRC_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
)

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
message("Include dirs are: ${dirs}")


# Common source files
file(GLOB SOURCE_FILES ${SRC_DIR}/*.c *.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})

#zlib
# include (${VENDOR_DIR}/ZlibInclude.cmake) # Add zlib library
# Conditional build or link of zlib
if(WIN32)
    file(GLOB ZLIB_SOURCES ${VENDOR_DIR}/zlib/*.c)
    add_library(my_zlib STATIC ${ZLIB_SOURCES})
    set_target_properties(my_zlib PROPERTIES LINKER_LANGUAGE C) # set zlib library language to C
else()
    set(ZLIB_SOURCE_DIR ${VENDOR_DIR}/zlib)
    add_custom_target(
        zlib_build
        COMMAND ${ZLIB_SOURCE_DIR}/configure
        COMMAND make -C ${ZLIB_SOURCE_DIR}
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
    set(ZLIB_LIBRARY "${ZLIB_SOURCE_DIR}/libz.a")
    include_directories(${ZLIB_SOURCE_DIR})
    add_dependencies(mscompress zlib_build)
endif()


#zstd
include (${VENDOR_DIR}/ZstdInclude.cmake) # Add zstd library
set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C) # set zstd library language to C

#yxml
include (${VENDOR_DIR}/YxmlInclude.cmake) # Add yxml library

#base64
include (${VENDOR_DIR}/Base64SetEnvFlags.cmake) # set compiler flags
include (${VENDOR_DIR}/Base64Include.cmake) # Add base64 library

# Define an option to choose between library and executable
option(BUILD_SHARED_LIB "Build shared library (node-addon)" ON)


# Create the shared library (node-addon)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "" SUFFIX ".node")

# Linking stage
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} zstd yxml base64 my_zlib)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_JS_LIB} zstd yxml base64 ${ZLIB_LIBRARY})
endif()

get_target_property(TMP_TARGET_LINK_LIBRARIES ${PROJECT_NAME} LINK_LIBRARIES)
message("Link libraries of target ${PROJECT_NAME}: ${TMP_TARGET_LINK_LIBRARIES}")


# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
add_definitions(-DNAPI_VERSION=3)
