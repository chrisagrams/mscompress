cmake_minimum_required(VERSION 3.15)

project (mscompress  VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} ${CMAKE_JS_SRC})

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

# Define an option to choose between library and executable
option(BUILD_SHARED_LIB "Build shared library (node-addon)" ON)

# Common source files
file(GLOB SOURCE_FILES ${SRC_DIR}/*.c wrapper.cpp)


# Create the shared library (node-addon)
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
