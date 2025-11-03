set(ZLIB_SOURCE_DIR ${VENDOR_DIR}/zlib)
# Add a custom target to run nmake and build zlib
if(WIN32)
    add_custom_target(
        zlib_build
        COMMAND nmake -f ${ZLIB_SOURCE_DIR}/win32/Makefile.msc
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
elseif(APPLE)
    # On macOS, define fdopen to prevent zlib's macro redefinition conflict
    add_custom_target(
        zlib_build
        COMMAND env CFLAGS=-Dfdopen=fdopen ${ZLIB_SOURCE_DIR}/configure
        COMMAND make -C ${ZLIB_SOURCE_DIR}
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
else()
    add_custom_target(
        zlib_build
        COMMAND ${ZLIB_SOURCE_DIR}/configure
        COMMAND make -C ${ZLIB_SOURCE_DIR}
        WORKING_DIRECTORY ${ZLIB_SOURCE_DIR}
    )
endif()
# Set the location of the built zlib library
if(WIN32)
    set(ZLIB_LIBRARY "${ZLIB_SOURCE_DIR}/zlib.lib")
else()
    set(ZLIB_LIBRARY "${ZLIB_SOURCE_DIR}/libz.a")
endif()

include_directories(${ZLIB_SOURCE_DIR})
add_dependencies(mscompress zlib_build)
