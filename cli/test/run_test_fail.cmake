# CMake script to run mscompress tests that are expected to FAIL gracefully
# (non-zero exit, human-readable error message, no crash, no leftover output).
#
# Expected variables:
# EXECUTABLE: Path to mscompress executable
# ARGS: List of arguments to pass to the executable
# INPUT_FILE: Path to input file
# TEMP_DIR: Path to temporary directory
# EXPECTED_PATTERN: Regex that must appear in stderr for the failure to count
#                   as a *graceful*, diagnosable rejection.

if(NOT DEFINED EXPECTED_PATTERN)
    message(FATAL_ERROR "EXPECTED_PATTERN must be defined for fail-mode tests")
endif()

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

set(ARGS_LIST ${ARGS})

message(STATUS "Running (expect graceful failure): ${EXECUTABLE} ${ARGS_LIST} ${INPUT_FILE} ${OUTPUT_FILE}")

execute_process(
    COMMAND ${EXECUTABLE} ${ARGS_LIST} ${INPUT_FILE} ${OUTPUT_FILE}
    RESULT_VARIABLE CMD_RESULT
    OUTPUT_VARIABLE CMD_OUTPUT
    ERROR_VARIABLE CMD_ERROR
)

# The command must fail, but not by dying to a signal (negative result on
# Unix). A signal death (SIGFPE/SIGSEGV/...) is a crash, not a graceful error.
if(CMD_RESULT EQUAL 0)
    message(STATUS "Command output: ${CMD_OUTPUT}")
    message(STATUS "Command error: ${CMD_ERROR}")
    message(FATAL_ERROR "Command unexpectedly succeeded (expected graceful failure)")
endif()

if(CMD_RESULT LESS 0)
    message(FATAL_ERROR "Command crashed with signal ${CMD_RESULT} instead of failing gracefully")
endif()

# The failure must be diagnosable: stderr must contain the expected message.
if(NOT CMD_ERROR MATCHES "${EXPECTED_PATTERN}")
    message(STATUS "Command output: ${CMD_OUTPUT}")
    message(STATUS "Command error: ${CMD_ERROR}")
    message(FATAL_ERROR "Command failed (exit ${CMD_RESULT}) but stderr does not match '${EXPECTED_PATTERN}'. Failure is silent/undiagnosable.")
endif()

# A failed compression must not leave a partial .msz behind.
if(EXISTS "${OUTPUT_FILE}")
    message(FATAL_ERROR "Failed compression left behind an output file: ${OUTPUT_FILE}")
endif()

message(STATUS "Test passed! (graceful failure, exit ${CMD_RESULT}, message matched '${EXPECTED_PATTERN}')")
