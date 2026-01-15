# CMake Tests for mscompress CLI

set(TEST_DATA_DIR ${CMAKE_SOURCE_DIR}/python/test/data)

# Simple test to run the executable with --version and --help
add_test(NAME VersionTest COMMAND mscompress --version)

add_test(NAME HelpTest COMMAND mscompress --help)


# Test compression and decompression using the run_test.cmake script
add_test(NAME CompressTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DMODE=compress
    -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
    -DEXPECTED_FILE=${TEST_DATA_DIR}/test.msz
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_compress
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
)

add_test(NAME DecompressTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DMODE=decompress
    -DINPUT_FILE=${TEST_DATA_DIR}/test.msz
    -DEXPECTED_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_decompress
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
)
