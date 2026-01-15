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

# Helper macro to add simple tests
macro(add_cli_test TEST_NAME ARGS)
    add_test(NAME ${TEST_NAME}
        COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:mscompress>
        "-DARGS=${ARGS}"
        -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
        -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_${TEST_NAME}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
    )
endmacro()

# --target-xml-format tests
add_cli_test(CompressDecompressTargetXML_zstd "--target-xml-format;zstd")
add_cli_test(CompressDecompressTargetXML_lz4 "--target-xml-format;lz4")
add_cli_test(CompressDecompressTargetXML_none "--target-xml-format;none")

# --target-mz-format tests
add_cli_test(CompressDecompressTargetMZ_zstd "--target-mz-format;zstd")
add_cli_test(CompressDecompressTargetMZ_lz4 "--target-mz-format;lz4")
add_cli_test(CompressDecompressTargetMZ_none "--target-mz-format;none")

# --target-inten-format tests
add_cli_test(CompressDecompressTargetInten_zstd "--target-inten-format;zstd")
add_cli_test(CompressDecompressTargetInten_lz4 "--target-inten-format;lz4")
add_cli_test(CompressDecompressTargetInten_none "--target-inten-format;none")

# --mz-lossy tests
add_cli_test(CompressDecompressMZLossy_cast "--mz-lossy;cast")
add_cli_test(CompressDecompressMZLossy_cast16 "--mz-lossy;cast16")
add_cli_test(CompressDecompressMZLossy_log "--mz-lossy;log")
add_cli_test(CompressDecompressMZLossy_delta16 "--mz-lossy;delta16")
add_cli_test(CompressDecompressMZLossy_delta24 "--mz-lossy;delta24")
add_cli_test(CompressDecompressMZLossy_delta32 "--mz-lossy;delta32")
add_cli_test(CompressDecompressMZLossy_vdelta16 "--mz-lossy;vdelta16")
add_cli_test(CompressDecompressMZLossy_vdelta24 "--mz-lossy;vdelta24")
add_cli_test(CompressDecompressMZLossy_vbr "--mz-lossy;vbr")
add_cli_test(CompressDecompressMZLossy_bitpack "--mz-lossy;bitpack")

# --int-lossy tests
add_cli_test(CompressDecompressIntLossy_cast "--int-lossy;cast")
add_cli_test(CompressDecompressIntLossy_cast16 "--int-lossy;cast16")
add_cli_test(CompressDecompressIntLossy_log "--int-lossy;log")
add_cli_test(CompressDecompressIntLossy_delta16 "--int-lossy;delta16")
add_cli_test(CompressDecompressIntLossy_delta24 "--int-lossy;delta24")
add_cli_test(CompressDecompressIntLossy_delta32 "--int-lossy;delta32")
add_cli_test(CompressDecompressIntLossy_vdelta16 "--int-lossy;vdelta16")
add_cli_test(CompressDecompressIntLossy_vdelta24 "--int-lossy;vdelta24")
add_cli_test(CompressDecompressIntLossy_vbr "--int-lossy;vbr")
add_cli_test(CompressDecompressIntLossy_bitpack "--int-lossy;bitpack")