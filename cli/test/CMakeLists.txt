# CMake Tests for mscompress CLI

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/data)

# Unit tests for the vendor raw -> mzML writer. Runs without libraw2ms:
# spectra are synthesized and checked against mscompress's own detectors.
file(GLOB RAW_WRITER_CORE_SOURCES ${SRC_DIR}/*.c ${SRC_DIR}/algos/*.c)
add_executable(test_raw_writer test_raw_writer.c ${RAW_WRITER_CORE_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../raw_input.c)
target_include_directories(test_raw_writer PUBLIC
    ${VENDOR_DIR}/zstd/lib
    ${VENDOR_DIR}/base64/include
    ${VENDOR_DIR}/base64/lib
    ${VENDOR_DIR}/yxml
    ${VENDOR_DIR}/lz4/include
    ${SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
if(WIN32)
    target_link_libraries(test_raw_writer PRIVATE base64 lz4 ${ZLIB_LIBRARY} zstd yxml)
else()
    target_link_libraries(test_raw_writer PRIVATE base64 lz4 ${ZLIB_LIBRARY} zstd yxml m Threads::Threads ${CMAKE_DL_LIBS})
endif()
add_dependencies(test_raw_writer zlib_build)
add_test(NAME RawWriterTest COMMAND test_raw_writer)

# Simple test to run the executable with --version and --help
add_test(NAME VersionTest COMMAND mscompress --version)

add_test(NAME HelpTest COMMAND mscompress --help)

add_test(NAME ListAlgorithmsTest COMMAND mscompress --list-algorithms)

# --json flag tests
add_test(NAME JsonVersionTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS=--json;--version"
    -DEXPECTED_PATTERN=version
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_json.cmake
)

add_test(NAME JsonListAlgorithmsTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS=--json;--list-algorithms"
    -DEXPECTED_PATTERN=name
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_json.cmake
)

add_test(NAME JsonDescribeTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS=--json;-d"
    -DINPUT_FILE=${TEST_DATA_DIR}/test.msz
    -DEXPECTED_PATTERN=num_spectra
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_json.cmake
)

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

add_test(NAME MSZXDecompressTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DINPUT_FILE=${TEST_DATA_DIR}/mszx/test.mszx
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_mszx
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_mszx.cmake
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

add_cli_test(CompressDecompressThreadsExceedSpectra "--threads;100")

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

# Test mzML files without scan= attribute (e.g. Sciex TTOF6600: sample/period/cycle/experiment IDs)
# Regression test for GitHub issue #103
add_test(NAME CompressSciexNoScanAttr
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS="
    -DINPUT_FILE=${TEST_DATA_DIR}/sciex_ttof6600_100.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_sciex_no_scan
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
)

# Corrupt base64 test - regression test for GitHub issue #111
# Verifies compress/decompress handles mzML with invalid base64 characters gracefully
add_test(NAME CompressCorruptBase64
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS="
    -DINPUT_FILE=${TEST_DATA_DIR}/corrupt_base64.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_corrupt_base64
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
)

# Chromatogram-only mzML (zero spectra, e.g. SRM runs from PXD020483).
# Regression test: total_spec == 0 used to SIGFPE in create_divisions.
# Mirrors the Python/Node behavior of clamping to a single division so the
# file roundtrips as XML-only mzML data.
add_test(NAME CompressChromatogramOnly
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS="
    -DINPUT_FILE=${TEST_DATA_DIR}/edge_cases/chromatogram_only.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_chromatogram_only
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
)

# mzML with an empty spectrumList and no binary data arrays (e.g. empty RAW
# converted by ThermoRawFileParser, or a truncated file).
# Regression test: pattern_detect returns NULL and the CLI used to exit 1
# without any error message.
add_test(NAME CompressNoBinaryArrays
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS="
    -DINPUT_FILE=${TEST_DATA_DIR}/edge_cases/no_binary_arrays.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_no_binary_arrays
    -DEXPECTED_PATTERN=format|spectra
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_fail.cmake
)

# Verbose mode test
add_test(NAME VerboseOutputTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_verbose
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_verbose.cmake
)

# Vendor raw -> msz roundtrip. Vendor files are too large for the repo, so
# the test self-skips unless MSCOMPRESS_TEST_RAW and RAW2MS_LIBRARY are set
# in the environment when ctest runs.
add_test(NAME RawCompressTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_raw
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_raw.cmake
)