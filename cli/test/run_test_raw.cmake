# CMake script to test vendor raw -> msz conversion (env-gated).
#
# Vendor files are far too large to keep in the repo, so this test only runs
# when both are provided at ctest time:
#   MSCOMPRESS_TEST_RAW=/path/to/file.(raw|.d|.wiff|.wiff2|.t2d)
#   RAW2MS_LIBRARY=/path/to/libraw2ms_capi.so
# Otherwise it reports itself as skipped (passes without doing work).
#
# Expected variables:
# EXECUTABLE: Path to mscompress executable
# TEMP_DIR: Path to temporary directory

if(NOT DEFINED ENV{MSCOMPRESS_TEST_RAW} OR NOT DEFINED ENV{RAW2MS_LIBRARY})
    message(STATUS "RawCompressTest skipped: set MSCOMPRESS_TEST_RAW and RAW2MS_LIBRARY to enable")
    return()
endif()

set(INPUT_FILE "$ENV{MSCOMPRESS_TEST_RAW}")
set(RAW2MS_LIB "$ENV{RAW2MS_LIBRARY}")

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()
if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()
if(NOT EXISTS "${RAW2MS_LIB}")
    message(FATAL_ERROR "RAW2MS_LIBRARY not found: ${RAW2MS_LIB}")
endif()

if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

get_filename_component(INPUT_NAME "${INPUT_FILE}" NAME_WE)
set(COMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}.msz")
set(DECOMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}.mzML")
set(RECOMPRESSED_FILE "${TEMP_DIR}/${INPUT_NAME}_recompressed.msz")
set(DECOMPRESSED_FILE2 "${TEMP_DIR}/${INPUT_NAME}_2.mzML")

message(STATUS "Testing raw conversion (roundtrip)...")
message(STATUS "Input: ${INPUT_FILE}")
message(STATUS "raw2ms library: ${RAW2MS_LIB}")

# Step 1: Compress the vendor file directly.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "RAW2MS_LIBRARY=${RAW2MS_LIB}"
            "${EXECUTABLE}" "${INPUT_FILE}" "${COMPRESSED_FILE}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Compress output: ${CMD_OUTPUT}")
    message(STATUS "Compress error: ${CMD_ERROR}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Raw compress failed with return code: ${CMD_RESULT}")
endif()
if(NOT EXISTS "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Compressed file not created: ${COMPRESSED_FILE}")
endif()

# Step 2: The msz must describe real spectra (a generic/EXTERNAL conversion
# would produce an msz with num_spectra 0).
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "RAW2MS_LIBRARY=${RAW2MS_LIB}"
            "${EXECUTABLE}" --json -d "${COMPRESSED_FILE}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE DESCRIBE_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Describe error: ${CMD_ERROR}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Describe failed with return code: ${CMD_RESULT}")
endif()
if(NOT DESCRIBE_OUTPUT MATCHES "num_spectra")
    message(STATUS "Describe output: ${DESCRIBE_OUTPUT}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Describe output has no num_spectra field")
endif()
if(DESCRIBE_OUTPUT MATCHES "\"num_spectra\": 0[,}]")
    message(STATUS "Describe output: ${DESCRIBE_OUTPUT}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Compressed raw file contains zero spectra")
endif()
message(STATUS "Describe: ${DESCRIBE_OUTPUT}")

# Step 3: Decompress back to mzML.
execute_process(
    COMMAND "${EXECUTABLE}" "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Decompress output: ${CMD_OUTPUT}")
    message(STATUS "Decompress error: ${CMD_ERROR}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Decompress failed with return code: ${CMD_RESULT}")
endif()
if(NOT EXISTS "${DECOMPRESSED_FILE}")
    file(REMOVE "${COMPRESSED_FILE}")
    message(FATAL_ERROR "Decompressed file not created: ${DECOMPRESSED_FILE}")
endif()

# Step 4: The mzML must hold spectra.
file(READ "${DECOMPRESSED_FILE}" MZML_CONTENT)
string(FIND "${MZML_CONTENT}" "<spectrum " SPECTRUM_POS)
if(SPECTRUM_POS EQUAL -1)
    file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}")
    message(FATAL_ERROR "Decompressed mzML contains no <spectrum> element")
endif()

# Step 5: msz -> mzML -> msz -> mzML must be stable (the raw->mzML text is
# generated once, so both decompressions must agree byte for byte).
execute_process(
    COMMAND "${EXECUTABLE}" "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}"
    RESULT_VARIABLE CMD_RESULT OUTPUT_QUIET ERROR_VARIABLE CMD_ERROR
)
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Recompress error: ${CMD_ERROR}")
    file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}")
    message(FATAL_ERROR "Recompress failed with return code: ${CMD_RESULT}")
endif()
execute_process(
    COMMAND "${EXECUTABLE}" "${RECOMPRESSED_FILE}" "${DECOMPRESSED_FILE2}"
    RESULT_VARIABLE CMD_RESULT OUTPUT_QUIET ERROR_VARIABLE CMD_ERROR
)
if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Decompress2 error: ${CMD_ERROR}")
    file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}")
    message(FATAL_ERROR "Second decompress failed with return code: ${CMD_RESULT}")
endif()

file(MD5 "${DECOMPRESSED_FILE}" FIRST_MD5)
file(MD5 "${DECOMPRESSED_FILE2}" SECOND_MD5)
message(STATUS "First decompress MD5:  ${FIRST_MD5}")
message(STATUS "Second decompress MD5: ${SECOND_MD5}")

file(REMOVE "${COMPRESSED_FILE}" "${DECOMPRESSED_FILE}" "${RECOMPRESSED_FILE}" "${DECOMPRESSED_FILE2}")

if(NOT "${FIRST_MD5}" STREQUAL "${SECOND_MD5}")
    message(FATAL_ERROR "Roundtrip checksum mismatch! Decompressed mzML files differ.")
endif()

message(STATUS "Test passed!")
