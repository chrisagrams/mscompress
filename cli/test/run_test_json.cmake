# CMake script to test mscompress --json output
#
# Expected variables:
# EXECUTABLE: Path to mscompress executable
# ARGS: Arguments to pass (semicolon-separated)
# EXPECTED_PATTERN: A string that must appear in stdout
# INPUT_FILE: (optional) Input file to append to command

if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE}")
endif()

set(ARGS_LIST ${ARGS})

if(DEFINED INPUT_FILE AND EXISTS "${INPUT_FILE}")
    list(APPEND ARGS_LIST "${INPUT_FILE}")
endif()

message(STATUS "Running: ${EXECUTABLE} ${ARGS_LIST}")

execute_process(
    COMMAND ${EXECUTABLE} ${ARGS_LIST}
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

if(NOT CMD_RESULT EQUAL 0)
    message(STATUS "Command output: ${CMD_OUTPUT}")
    message(STATUS "Command error: ${CMD_ERROR}")
    message(FATAL_ERROR "Command failed with return code: ${CMD_RESULT}")
endif()

# Verify output contains expected pattern
string(FIND "${CMD_OUTPUT}" "${EXPECTED_PATTERN}" PATTERN_POS)
if(PATTERN_POS EQUAL -1)
    message(STATUS "Output was: ${CMD_OUTPUT}")
    message(FATAL_ERROR "Expected pattern '${EXPECTED_PATTERN}' not found in output")
endif()

# Verify output starts with { or [ (valid JSON)
string(STRIP "${CMD_OUTPUT}" STRIPPED_OUTPUT)
string(SUBSTRING "${STRIPPED_OUTPUT}" 0 1 FIRST_CHAR)
if(NOT FIRST_CHAR STREQUAL "{" AND NOT FIRST_CHAR STREQUAL "[")
    message(STATUS "Output was: ${CMD_OUTPUT}")
    message(FATAL_ERROR "Output does not start with '{' or '[' — not valid JSON")
endif()

message(STATUS "JSON output validated successfully")
message(STATUS "Test passed!")
