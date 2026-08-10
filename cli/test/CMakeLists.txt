# CMake Tests for mscompress CLI

set(TEST_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../test/data)

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

# Non-indexed mzML (no <indexedmzML> wrapper, e.g. Waters exports) with a lossy
# scheme requested - regression test for GitHub issue #176 (segfault caused by
# misclassifying non-indexed mzML as an external file, which led to a NULL
# compression function pointer being invoked).
add_test(NAME CompressNonIndexedMzMLLossy
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS=--mz-lossy;delta32"
    -DINPUT_FILE=${TEST_DATA_DIR}/nonindexed_test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_nonindexed_mzml_lossy
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
)

# Verbose mode test
add_test(NAME VerboseOutputTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_verbose
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_verbose.cmake
)
# --- byte shuffle -----------------------------------------------------------

# A --shuffle file must decompress byte-identically, differ from the
# unshuffled output, and be smaller.
add_test(NAME ShuffleRoundTripTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_shuffle
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_shuffle.cmake
)

# test.msz predates the shuffle, so it has the marker bit clear and must still
# decompress to exactly test.mzML. Guards the footer flag-bit scheme.
add_test(NAME ShuffleBackwardCompatTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DMODE=decompress
    -DINPUT_FILE=${TEST_DATA_DIR}/test.msz
    -DEXPECTED_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_shuffle_comorbid
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
)

# A checked-in shuffled file (msz 0.2), so the decode path is tested against
# committed bytes rather than only against whatever this build just wrote.
add_test(NAME ShuffleFixtureDecompressTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    -DMODE=decompress
    -DINPUT_FILE=${TEST_DATA_DIR}/test_shuffled.msz
    -DEXPECTED_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_shuffle_fixture
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
)

# A lossy stream has no fixed post-transform element width, so --shuffle must be
# skipped for it rather than corrupting the stream.
add_test(NAME ShuffleWithLossyTest
    COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:mscompress>
    "-DARGS=--shuffle;--mz-lossy;delta32"
    -DINPUT_FILE=${TEST_DATA_DIR}/test.mzML
    -DTEMP_DIR=${CMAKE_CURRENT_BINARY_DIR}/tmp_shuffle_lossy
    -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test_simple.cmake
)
