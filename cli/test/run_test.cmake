# CMake script to run mscompress tests

# Expected variables:
# EXECUTABLE: Path to mscompress executable
# MODE: "compress" or "decompress"
# INPUT_FILE: Path to input file
# EXPECTED_FILE: Path to expected output file
# TEMP_DIR: Path to temporary directory

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

get_filename_component(INPUT_NAME "${INPUT_FILE}" NAME_WE)

if("${MODE}" STREQUAL "compress")
    set(OUTPUT_FILE "${TEMP_DIR}/${INPUT_NAME}.msz")
elseif("${MODE}" STREQUAL "decompress")
    set(OUTPUT_FILE "${TEMP_DIR}/${INPUT_NAME}.mzML")
else()
    message(FATAL_ERROR "Unknown mode: ${MODE}")
endif()

message(STATUS "Testing ${MODE}...")
message(STATUS "Input: ${INPUT_FILE}")
message(STATUS "Expected: ${EXPECTED_FILE}")
message(STATUS "Output: ${OUTPUT_FILE}")

# Run command
execute_process(
    COMMAND "${EXECUTABLE}" "${INPUT_FILE}" "${OUTPUT_FILE}"
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Command output: ${CMD_OUTPUT}")
    message(STATUS "Command error: ${CMD_ERROR}")
    file(REMOVE "${OUTPUT_FILE}")
    message(FATAL_ERROR "Command failed with return code: ${CMD_RESULT}")
endif()

if(NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "Output file not found: ${OUTPUT_FILE}")
endif()

# Verify result
file(MD5 "${EXPECTED_FILE}" EXPECTED_MD5)
file(MD5 "${OUTPUT_FILE}" ACTUAL_MD5)

message(STATUS "Expected MD5: ${EXPECTED_MD5}")
message(STATUS "Actual MD5:   ${ACTUAL_MD5}")

# Cleanup
file(REMOVE "${OUTPUT_FILE}")

if(NOT "${EXPECTED_MD5}" STREQUAL "${ACTUAL_MD5}")
    message(FATAL_ERROR "Checksum mismatch!")
endif()

message(STATUS "Test passed!")
