# CMake script to run simple mscompress tests (check exit code and output file size)

# Expected variables:
# EXECUTABLE: Path to mscompress executable
# ARGS: List of arguments to pass to the executable
# INPUT_FILE: Path to input file
# TEMP_DIR: Path to temporary directory

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "Input file not found: ${INPUT_FILE}")
endif()

# Ensure temp dir exists
if(NOT EXISTS "${TEMP_DIR}")
    file(MAKE_DIRECTORY "${TEMP_DIR}")
endif()

set(OUTPUT_FILE "${TEMP_DIR}/output.msz")

# Clean previous output
if(EXISTS "${OUTPUT_FILE}")
    file(REMOVE "${OUTPUT_FILE}")
endif()

# Parse ARGS from semicolon-separated string to list
set(ARGS_LIST ${ARGS})

message(STATUS "Running: ${EXECUTABLE} ${ARGS_LIST} ${INPUT_FILE} ${OUTPUT_FILE}")

execute_process(
    COMMAND ${EXECUTABLE} ${ARGS_LIST} ${INPUT_FILE} ${OUTPUT_FILE}
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Command output: ${CMD_OUTPUT}")
    message(STATUS "Command error: ${CMD_ERROR}")
    message(FATAL_ERROR "Command failed with return code: ${CMD_RESULT}")
endif()

if(NOT EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "Output file was not created: ${OUTPUT_FILE}")
endif()

file(SIZE "${OUTPUT_FILE}" FILE_SIZE)
message(STATUS "Output file size: ${FILE_SIZE} bytes")

if(FILE_SIZE EQUAL 0)
    message(FATAL_ERROR "Output file is empty.")
endif()

# Decompress validation
set(DECOMPRESSED_FILE "${TEMP_DIR}/output.mzML")

message(STATUS "Running decompression: ${EXECUTABLE} ${OUTPUT_FILE} ${DECOMPRESSED_FILE}")

execute_process(
    COMMAND ${EXECUTABLE} ${OUTPUT_FILE} ${DECOMPRESSED_FILE}
    RESULT_VARIABLE DECOMP_CMD_RESULT
    OUTPUT_VARIABLE DECOMP_CMD_OUTPUT
    ERROR_VARIABLE DECOMP_CMD_ERROR
)

if(NOT DECOMP_CMD_RESULT EQUAL 0)
    message(STATUS "Decompression output: ${DECOMP_CMD_OUTPUT}")
    message(STATUS "Decompression error: ${DECOMP_CMD_ERROR}")
    message(FATAL_ERROR "Decompression command failed with return code: ${DECOMP_CMD_RESULT}")
endif()

if(NOT EXISTS "${DECOMPRESSED_FILE}")
    message(FATAL_ERROR "Decompressed file was not created: ${DECOMPRESSED_FILE}")
endif()

file(SIZE "${DECOMPRESSED_FILE}" DECOMP_FILE_SIZE)
message(STATUS "Decompressed file size: ${DECOMP_FILE_SIZE} bytes")

if(DECOMP_FILE_SIZE EQUAL 0)
    message(FATAL_ERROR "Decompressed file is empty.")
endif()

# Cleanup
file(REMOVE "${OUTPUT_FILE}")
file(REMOVE "${DECOMPRESSED_FILE}")

message(STATUS "Test passed!")
